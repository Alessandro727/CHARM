/* scheduler.h */
#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <vector>
#include <functional>
#include <iostream>
#include <atomic> // Necessario per std::atomic

#include "scheduler_impl.h" 

#include "tasking/task_queue.h"
#include "utils/utils.h"
#include "comm/mpienv.h"

namespace Charm {

// Forward declaration per Condition
class Condition; 

// --- DICHIARAZIONI EXTERN -----------------
extern thread_local int64_t task_created_cnt;
extern thread_local int64_t heap_task_created_cnt;
// -------------------------------------------

extern bool global_exit_flag;
extern thread_local bool allow_yield;
extern thread_local int thread_id; 

// Wrapper Worker
class Worker {
public:
  Worker(WorkerImpl* impl) : pImpl(impl) {}

  inline void yield() { pImpl->yield(); }
  inline void maybe_yield() { pImpl->maybe_yield(); }
  inline void idle() { pImpl->idle(); }
  inline void wait(Condition* c) { pImpl->wait(c); }
  inline void signal(Condition* c) { pImpl->signal(c); }
  inline void spawn_coroutine(std::function<void()> f, int tag = 0) { pImpl->spawn_coroutine(f, tag); }
  inline void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) { pImpl->spawn_coroutine_periodic(f, tag); }
  inline void await() { pImpl->await(); }
  inline void finish() { pImpl->finish(); }

  // Template Method
  template<typename F>
  void private_enqueue(F f){
    TaskQueue& tq = pImpl->get_task_queue();
    if( sizeof(f) > sizeof(arg_pack_t) ){
      heap_task_created_cnt++; 
      F * tp = new F(f);
      tq.spawn_private(task_heapfunctor_proxy<F>, tp, tp, tp);
    }else{
      task_created_cnt++; 
      uint64_t args[3] = {0};
      char* copyf = reinterpret_cast<char*>(&f);
      char* fargs = reinterpret_cast<char*>(&args);
      memcpy(fargs, copyf, sizeof(f));
      tq.spawn_private(task_functor_proxy<F>, args[0],args[1],args[2]); 
    }
  }

  WorkerImpl* get_impl() { return pImpl; }

private:
  WorkerImpl* pImpl;
};

// Wrapper Scheduler
class Scheduler {
public:
  Scheduler(int worker_num);
  ~Scheduler();

  void await();
  void finish();
  size_t get_size();
  int get_id();
  
  Worker* get_cur_worker();
  Worker* get_worker(size_t index);

  void start();

  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0);
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0);
  void start_perf_counters();

  std::vector<Worker*>& get_workers() { return workers; }

  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

  std::string get_active_scheduler_type();

private:
  SchedulerImpl* pImpl;
  std::vector<Worker*> workers;
  size_t num_threads;
};

extern Scheduler * global_scheduler;
void yield();
void maybe_yield();

inline size_t thread_rank(){ return global_scheduler->get_id(); }
inline size_t thread_size(){ return global_scheduler->get_size(); }
inline void set_allow_yield(bool val){ allow_yield = val; }
inline size_t my_id(){ return mpi_env->rank * THREAD_SIZE + thread_rank(); }
template <typename F>
inline void delivery_private(F f){ global_scheduler->private_enqueue(thread_rank(), f); }

}
#endif