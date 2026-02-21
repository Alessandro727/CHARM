/* scheduler_RING.h */
#ifndef _SCHEDULER_RING_H_
#define _SCHEDULER_RING_H_

#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <queue>
#include <iostream>
#include <future>

#include "scheduler_impl.h"
#include "sched/coroutine.h"
#include "sched/thread_barrier.h"
#include "tasking/task_queue.h"
#include "utils/timestamp.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "comm/mpienv.h"


namespace Charm{

class RingWorker : public WorkerImpl {
public:

  RingWorker( size_t ith, 
          int worker_num,
          Thread_Barrier * tb );

  void task_worker();

  void private_enqueue_impl(std::function<void()> f) override {}
  void yield() override { csched->coroutine_yield(); }
  void maybe_yield() override { csched->coroutine_maybe_yield(); }
  void idle() override { csched->coroutine_idle(); }
  void wait(Condition* c) override { csched->coroutine_wait(c); }
  void signal(Condition* c) override { csched->coroutine_signal(c); }
  void spawn_coroutine(std::function<void()> f, int tag = 0) override { csched->coroutine_create(f, tag); }
  void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) override { csched->coroutine_create_periodic(f, tag); }
  void await() override { t.join(); }
  void finish() override { this->stop = true; }
  TaskQueue& get_task_queue() override { return taskQ; }

  // Helper interno
  inline void add_task_worker(int num){
    while(num-- > 0){
      spawn_coroutine([this]{
        this->task_worker();
      });
    }
  }

  // Helper interno template
  template<typename F>
  void private_enqueue(F f){
    if( sizeof(f) > sizeof(arg_pack_t) ){
      heap_task_created_cnt ++;
      F * tp = new F(f);
      taskQ.spawn_private(task_heapfunctor_proxy<F>, tp, tp, tp);
    }else{
      task_created_cnt ++;
      uint64_t args[3] = {0};
      char* copyf = reinterpret_cast<char*>(&f);
      char* fargs = reinterpret_cast<char*>(&args);
      memcpy(fargs, copyf, sizeof(f));
      taskQ.spawn_private(task_functor_proxy<F>, args[0],args[1],args[2]); 
    }
  }
 
private:
  size_t rank;
  bool stop;
  std::thread t;
  Coro_Scheduler * csched;
  TaskQueue taskQ;
  std::mutex l_mtx, r_mtx;
};


class RingScheduler : public SchedulerImpl {
public:

  RingScheduler(int worker_num);
  void start() override { start_barrier->wait(); }
  void finish() override;
  void await() override;
  
  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0) override;
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0) override;

  std::vector<WorkerImpl*> get_workers_impl() override;

  /* enqueue a private task */
  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

private:
  size_t num_threads;
  std::vector<RingWorker*> workers;
  Thread_Barrier * start_barrier;
};

} // namespace Charm
#endif