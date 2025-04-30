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

Worker::Worker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<Worker*>& all_workers, std::vector<std::unique_ptr<TaskInfo>>& tasks)
    : rank(ith), stop(false), all_workers(all_workers) {
    current_chiplet = ith / CORES_PER_CHIPLET;


  
    t = std::thread( [ith, this, tb, worker_num](){
      this->csched = new Coro_Scheduler(ith, &taskQ);
      this->stop = false;


  #ifdef NUMA_AWARE
      int threads = THREAD_SIZE;
  #endif
  
      thread_id = ith;
      tb->wait();
  
      /* create normal workers */
      this->add_task_worker(worker_num);
  
      csched->await();
    });

    ti = std::unique_ptr<TaskInfo>(new TaskInfo());
    int i = worker_num;
    // Create tasks
    std::unique_ptr<TaskInfo> ti(new TaskInfo);
    ti->id = i;
    ti->tid.store(0);
    ti->assignedSocket = i % NUMA_DOMAINS;
    ti->originalSocket = ti->assignedSocket;
    ti->assignedCore = i;
    ti->l2miss_value = -1;
    ti->l3hit_value = -1;
    ti->l3miss_value = -1;
    ti->llcMiss_value = -1;
    ti->remoteFwd_value = -1;
    ti->remoteHitm_value = -1;
    ti->remoteDram_value = -1;
    ti->lastMigration = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    // Allocate 1 MB buffer on the
    size_t numDoubles = (1 << 20) / sizeof(double);
    ti->bufSize = numDoubles;
    ti->buffer = (double *)numa_alloc_onnode(numDoubles * sizeof(double),
                                             ti->assignedSocket);
    if (!ti->buffer)
    {
      std::cerr << "[Error] numa_alloc_onnode failed for task " << ith << "\n";
    }
    for (size_t k = 0; k < numDoubles; k++)
    {
      ti->buffer[k] = (double)rand() / RAND_MAX;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Open perf events for each task
    pid_t ttid = ti->tid.load();
    this->eventsCounter = new PerfCounter();
    this->eventsCounter->startCounters();
    this->time = std::chrono::steady_clock::now();

    tasks.push_back(std::move(ti));
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

  std::srand((unsigned)std::time(nullptr));

    //<<< ADDED FOR AMD SUPPORT >>>: Detect vendor and set raw codes
    std::string vendor = detectCpuVendor();
    if (vendor == "GenuineIntel") {
        // Original Intel placeholders
        gEvt = {
            0x3424, // L2_MISS
            0x3425, // L3_HIT
            0x3426, // L3_MISS
            0x412E, // LLC_MISSES
            0x01b7, // REMOTE_FWD
            0x02b7, // REMOTE_HITM
            0x01cb  // REMOTE_DRAM
        };
        std::cout << "[Info] Detected Intel CPU: using Intel raw events.\n";
    }
    else if (vendor == "AuthenticAMD") {
        // Example: we fallback to 0 here and will open "generic" counters
        // for the L2/L3/LLC events. Real AMD raw codes differ by microarchitecture
        // and you must consult the AMD PPR or other docs for correct event+Umask.
        gEvt = {
            0, 0, 0, 0,  // these will fallback to PERF_COUNT_HW_CACHE_MISSES
            0, 0, 0      // we have no direct AMD codes for REMOTE_FWD/HITM/DRAM here
        };
        std::cout << "[Info] Detected AMD CPU: fallback to generic counters.\n";
    }
    else {
        // Some other vendor (VM, etc.?). Use all generic or do your own logic
        gEvt = {0,0,0,0,0,0,0};
        std::cout << "[Warning] Unknown CPU vendor: using all-generic.\n";
    }

  tasks.reserve(THREAD_SIZE);
  
  /* no need to use emplace_back */
  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new Worker(i, worker_num, start_barrier, workers, tasks));
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
  runSAM(global_scheduler->tasks);
}

void maybe_yield(){
  ASSERT_CHARM(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->maybe_yield();
}

}//namespace Charm
