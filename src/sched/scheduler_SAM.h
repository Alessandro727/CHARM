/* scheduler_SAM.h */
#ifndef _SCHEDULER_SAM_H_
#define _SCHEDULER_SAM_H_

#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <queue>
#include <iostream>
#include <future>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cmath>
#include <numa.h>
#include <numaif.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <cpuid.h>

#include "scheduler_impl.h" 
#include "sched/coroutine.h"
#include "sched/thread_barrier.h"
#include "tasking/task_queue.h"
#include "utils/timestamp.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "comm/mpienv.h"
#include "perf/perf_counter.h"

// --- NUOVI DEFINE SPECIFICI PER SAM ---
#define CORES_PER_NUMA_NODE_SAM 64
#define NUMA_DOMAINS_SAM 2
#define SCHEDULER_TIMER_SAM 550 
// --------------------------------------

namespace Charm {

class SamScheduler; 

struct RawEventCodes {
    uint64_t l2miss;
    uint64_t l3hit;
    uint64_t l3miss;
    uint64_t llcMiss;
    uint64_t remoteFwd;
    uint64_t remoteHitm;
    uint64_t remoteDram;
};

struct CoherenceMetrics {
    double l2miss;
    double l3hit;
    double l3miss;
    double llcMiss;
    double remoteFwd;
    double remoteHitm;
    double remoteDram;
    double intraSocket;  
    double interSocket;  
};

struct TaskInfo {
    int id;
    std::thread::native_handle_type threadHandle; 
    std::atomic<pid_t> tid;
    int assignedSocket; 
    int assignedCore;   
    int originalSocket; 
    
    CoherenceMetrics metrics;
    double l2miss_value;
    double l3hit_value;
    double l3miss_value;
    double llcMiss_value;
    double remoteFwd_value;
    double remoteHitm_value;
    double remoteDram_value;
    
    double* buffer;
    size_t bufSize;
    std::chrono::steady_clock::time_point lastMigration;

    TaskInfo() : tid(0), buffer(nullptr) {} 
};

class SamWorker : public WorkerImpl {
public:
  SamWorker(size_t ith, 
            int worker_num,
            Thread_Barrier * tb,
            std::vector<SamWorker*>& all_workers,
            SamScheduler* scheduler);

  void task_worker();
  
  static void set_thread_affinity(std::thread::native_handle_type t_handle, int core_id);

  void yield() override; 
  void maybe_yield() override { csched->coroutine_maybe_yield(); }
  void idle() override { csched->coroutine_idle(); }
  void wait(Condition* c) override { csched->coroutine_wait(c); }
  void signal(Condition* c) override { csched->coroutine_signal(c); }
  void spawn_coroutine(std::function<void()> f, int tag = 0) override { csched->coroutine_create(f, tag); }
  void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) override { csched->coroutine_create_periodic(f, tag); }
  void await() override { t.join(); }
  void finish() override;
  TaskQueue& get_task_queue() override { return taskQ; }
  void private_enqueue_impl(std::function<void()> f) override {}

  inline void add_task_worker(int num){
    while(num-- > 0){
      spawn_coroutine([this]{
        this->task_worker();
      });
    }
  }

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

  PerfCounter* eventsCounter;
  std::chrono::steady_clock::time_point time;
  TaskInfo* ti; 

private:
  size_t rank;
  bool stop;
  std::thread t;
  Coro_Scheduler * csched;
  TaskQueue taskQ;
  std::vector<SamWorker*>& all_workers;
  SamScheduler* parent_scheduler;
};

class SamScheduler : public SchedulerImpl {
public:
  SamScheduler(int worker_num);
  
  void start() override { start_barrier->wait(); }
  void finish() override;
  void await() override;
  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0) override;
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0) override;
  std::vector<WorkerImpl*> get_workers_impl() override;

  void start_perf_counters();
  
  void runSAM(); 

  RawEventCodes gEvt;
  std::vector<std::unique_ptr<TaskInfo>> tasks;
  std::mutex samMutex;

  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

private:
  size_t num_threads;
  std::vector<SamWorker*> workers;
  Thread_Barrier * start_barrier;
};

} // namespace Charm
#endif