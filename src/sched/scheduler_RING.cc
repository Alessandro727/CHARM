/* scheduler_RING.cc */
#include <future>
#include <thread>
#include <atomic>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h"
#include "scheduler_RING.h"

namespace Charm{


RingWorker::RingWorker(size_t ith, int worker_num,  Thread_Barrier * tb) 
  : rank(ith), stop(false) {
  
  t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
    int sockets = numa_num_configured_nodes();
    int threads_per_node = threads / sockets;
    int sockets_id = ith / threads_per_node;
    int retval = numa_run_on_node(sockets_id);
    ASSERT_CHARM(retval == 0, "Failed to set thread affinity to target numa node");
#endif

    Charm::thread_id = ith;
    
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

void RingWorker::task_worker(){
  Task victim;
  bool finded = false;
  for(;;){
    finded = taskQ.try_private(&victim);
    if(!finded){
      if(!this->stop){
        idle();
      }else break;
    }else{
      csched->active_coro_num ++;
      victim();
      csched->active_coro_num --;
      maybe_yield();
    }
  }
}

RingScheduler::RingScheduler(int worker_num){
  num_threads = THREAD_SIZE;
  start_barrier = new Thread_Barrier( num_threads+1 );

  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back( new RingWorker(i, worker_num, start_barrier) );
  }
  while(start_barrier->get_cnt() != 1);
}

void RingScheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void RingScheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void RingScheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void RingScheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

std::vector<WorkerImpl*> RingScheduler::get_workers_impl() {
    std::vector<WorkerImpl*> base_workers;
    for(auto* w : workers) base_workers.push_back(w);
    return base_workers;
}

}//namespace Charm