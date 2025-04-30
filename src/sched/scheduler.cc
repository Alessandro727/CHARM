/* scheduler.cc
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * INSTITUTE OF COMPUTING TECHNOLOGY AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */



#include <future>
#include <thread>
#include <atomic>
#include <numaif.h>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h"
#include "sched/coroutine.h"
#include "tasking/task_queue.h"

namespace Charm{

Scheduler * global_scheduler;
bool global_exit_flag;

thread_local int thread_id;
thread_local bool allow_yield=true;

Worker::Worker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<Worker*>& all_workers)
    : rank(ith), stop(false), all_workers(all_workers) {
    current_chiplet = ith / CORES_PER_CHIPLET;
  
    t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
#endif

    this->eventsCounter = new PerfCounter();
    this->eventsCounter->startCounters();
    this->time = std::chrono::steady_clock::now();

    cpu_set_t cpuset;
    pthread_t current_thread = pthread_self();  // Get the current thread

    // Initialize the CPU set
    CPU_ZERO(&cpuset);
    CPU_SET(ith, &cpuset);

    // Set the affinity of the current thread to the specified CPU cores
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cout << "Error: Unable to set thread affinity!\n";
    }

    set_thread_affinity(rank);
    int numa_node = rank / CORES_PER_NUMA_NODE;

    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);

    thread_id = ith;
    tb->wait();

    /* create normal workers */
    this->add_task_worker(worker_num);
    
    csched->await();
    
  });

}


void Worker::task_worker(){
  Task victim;
  bool finded = false;
  for(;;){
    finded = taskQ.try_private(&victim);
    /* we may get a task here */
    if(!finded){
      if(!this->stop){
        //wait(this->have_tasks);
        idle();
      }else break;
    }else{
      csched->active_coro_num ++;
      victim();
      csched->active_coro_num --;
      /* to see if we should switch to a periodic worker */
      maybe_yield();
    }
  }
}

//   void Worker::task_worker() {
//     Task victim;
//     bool finded = false;
//     for(;;) {
//         finded = taskQ.try_private(&victim);
//         if(!finded) {
//             if(!this->stop) {
//                 if (!steal_from_neighbors()) {
//                     if (!attempt_steal_from_chiplet()) {
//                         attempt_steal_from_other_chiplets();
//                     }
//                 }
//                 if (!finded) {
//                     idle();
//                 }
//             } else break;
//         } else {
//             csched->active_coro_num++;
//             victim();
//             csched->active_coro_num--;
//             maybe_yield();
//         }
//     }
// }

// bool Worker::steal_from_neighbors() {
//     for (int i = 0; i < CORES_PER_CHIPLET; ++i) {
//         if (i != rank % CORES_PER_CHIPLET) {
//             Worker* neighbor = all_workers[current_chiplet * CORES_PER_CHIPLET + i];
//             Task stolen_task;
//             if (neighbor->taskQ.try_steal(&stolen_task)) {
//                 taskQ.add_stolen_task(stolen_task);
//                 return true;
//             }
//         }
//     }
//     return false;
// }

// void Worker::attempt_steal_from_other_chiplets() {
//     for (int chiplet = 0; chiplet < CHIPLETS; ++chiplet) {
//         if (chiplet != current_chiplet) {
//             for (int core = 0; core < CORES_PER_CHIPLET; ++core) {
//                 Worker* remote_worker = all_workers[chiplet * CORES_PER_CHIPLET + core];
//                 Task stolen_task;
//                 if (remote_worker->taskQ.try_steal(&stolen_task)) {
//                     taskQ.add_stolen_task(stolen_task);
//                     return;
//                 }
//             }
//         }
//     }
// }

// bool Worker::attempt_steal_from_chiplet() {
//     return steal_from_neighbors();
// }




void Worker::set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Error setting thread affinity: " << rc << std::endl;
    }
    int numa_node = core_id / CORES_PER_NUMA_NODE;
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);
}

   

Scheduler::Scheduler(int worker_num){
  num_threads = THREAD_SIZE;
  start_barrier = new Thread_Barrier( num_threads+1 );

  /* no need to use emplace_back */
  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new Worker(i, worker_num, start_barrier, workers));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  /* make sure that all the thread local memory are finished */
  while(start_barrier->get_cnt() != 1);
}

void Scheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void Scheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void Scheduler::start_perf_counters() {
  for(auto& w : workers){
    w->eventsCounter = new PerfCounter();
    w->eventsCounter->startCounters();
  }
}

size_t Scheduler::get_size(){
  return this->num_threads;
}

int Scheduler::get_id(){
  return thread_id;
}

void Scheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void Scheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

void yield(){
  ASSERT_CHARM(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->yield();
}

void maybe_yield(){
  ASSERT_CHARM(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->maybe_yield();
}

}//namespace Charm
