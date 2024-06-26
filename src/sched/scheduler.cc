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

Worker::Worker(size_t ith, int worker_num,  Thread_Barrier * tb) 
  : rank(ith), stop(false) {
  
  t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    //this->have_tasks = new Condition();
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
//    int threads = 8;
    int sockets = numa_num_configured_nodes();
    int threads_per_node = threads / sockets;
    int sockets_id = ith / threads_per_node;
//    int retval = numa_run_on_node(sockets_id);
//    std::cout << "NUMA Run retval = " << retval << std::endl;
    //ASSERT(retval == -1, "Failed to set thread affinity to target numa node");
#endif

    this->eventsCounter = new PerfCounter();
    this->eventsCounter->startCounters();

    thread_id = ith;
    tb->wait();

    /* create normal workers */
    this->add_task_worker(worker_num);
    
    csched->await();
    
  });

#ifndef NUMA_AWARE
  cpu_set_t cpuset;
  CPU_ZERO( &cpuset );
  CPU_SET( ith, &cpuset );
  pthread_setaffinity_np( t.native_handle(),  sizeof(cpuset), &cpuset );
#endif
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
}

   

Scheduler::Scheduler(int worker_num){
  num_threads = THREAD_SIZE;
  start_barrier = new Thread_Barrier( num_threads+1 );

  /* no need to use emplace_back */
  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back( new Worker(i, worker_num, start_barrier) );
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
  ASSERT(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->yield();
}

void maybe_yield(){
  ASSERT(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->maybe_yield();
}

}//namespace Charm
