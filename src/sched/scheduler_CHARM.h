/* scheduler_CHARM.h */
#ifndef _SCHEDULER_CHARM_H_
#define _SCHEDULER_CHARM_H_

#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <queue>
#include <iostream>
#include <future>
#include <chrono>
#include <numaif.h>

#include "scheduler_impl.h" // Include l'interfaccia
#include "sched/coroutine.h"
#include "sched/thread_barrier.h"
#include "tasking/task_queue.h"
#include "utils/timestamp.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "comm/mpienv.h"
#include "perf/perf_counter.h"

#define CHIPLETS_PER_NUMA_NODE 8
#define CORES_PER_CHIPLET 8
#define RMT_CHIP_ACCESS_RATE 300
#define CORES_PER_NUMA_NODE 64
#define NUMA_DOMAINS 2
#define TOTAL_CORES 128
#define TOTAL_CHIPLETS 16
#define SCHEDULER_TIMER 750 

namespace Charm{

class CharmWorker : public WorkerImpl {
public:

  CharmWorker( size_t ith, 
          int worker_num,
          Thread_Barrier * tb,
          std::vector<CharmWorker*>& all_workers);

  void task_worker();
  void set_thread_affinity(int core_id);

  void update_location() {
    if (spread_rate <= 0 || spread_rate > CHIPLETS_PER_NUMA_NODE || THREAD_SIZE > spread_rate * CORES_PER_CHIPLET) return; 
    int chunk_size = CORES_PER_CHIPLET / spread_rate;
    if (chunk_size < 1) chunk_size = 1;
    int machine_capacity = CHIPLETS_PER_NUMA_NODE * chunk_size;
    int pass = rank / machine_capacity; 
    current_chiplet = (rank / chunk_size) % CHIPLETS_PER_NUMA_NODE;
    current_slot = (rank % chunk_size) + (pass * chunk_size);
    int core = current_chiplet * CORES_PER_CHIPLET + current_slot;
    set_thread_affinity(core);
    int numa_node = core / CORES_PER_NUMA_NODE;
    numa_set_preferred(numa_node);
  }

  void increase_spread() {
    if (spread_rate < CHIPLETS_PER_NUMA_NODE) {
        spread_rate = spread_rate + 1;
        update_location();
    }
  }

  void decrease_spread() {
    if (spread_rate > 1) {
        spread_rate = spread_rate -1;
        update_location();
    }
  }

  int calculateCore(int core) {
    int base = (core % CHIPLETS_PER_NUMA_NODE) * CORES_PER_CHIPLET; 
    int cycle = core / CHIPLETS_PER_NUMA_NODE;       
    if (cycle % 2 == 0) {
        return base;
    } else {
        return base + 1;
    }
  }

  void spread() {
    if (currentChiplets + 1 <= TOTAL_CHIPLETS) {
      currentChiplets = currentChiplets + 1;
      assignBalanced();
    }
  }

  void compact() {
    if (currentChiplets - 1 > 0) {
      currentChiplets = currentChiplets - 1;
      assignBalanced();
    }
  }

  void assignBalanced() {
    int tasksPerChiplet = THREAD_SIZE / currentChiplets;
    int extraTasks = THREAD_SIZE % currentChiplets;
    int taskIndex = 0;
    for (int chiplet = 0; chiplet < currentChiplets; ++chiplet) {
      int chipletsTaskCount = tasksPerChiplet + (chiplet < extraTasks ? 1 : 0);
      for (int i = 0; i < chipletsTaskCount; ++i) {
        taskAssignmentsOnChips[taskIndex] = chiplet * CORES_PER_CHIPLET + i;
        taskIndex++;
      }
    }
    int core = taskAssignmentsOnChips[rank];
    set_thread_affinity(core);
  }

  void private_enqueue_impl(std::function<void()> f) override {}
  void yield() override;
  void maybe_yield() override { csched->coroutine_maybe_yield(); }
  void idle() override { csched->coroutine_idle(); }
  void wait(Condition* c) override { csched->coroutine_wait(c); }
  void signal(Condition* c) override { csched->coroutine_signal(c); }
  void spawn_coroutine(std::function<void()> f, int tag = 0) override { csched->coroutine_create(f, tag); }
  void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) override { csched->coroutine_create_periodic(f, tag); }
  void await() override { t.join(); }
  void finish() override { 
    this->stop = true;
    eventsCounter->stopCounters();
  }
  TaskQueue& get_task_queue() override { return taskQ; }

  // Helper interno (non override)
  inline void add_task_worker(int num){
    while(num-- > 0){
      spawn_coroutine([this]{
        this->task_worker();
      });
    }
  }

  // Helper interno per l'uso dentro la classe stessa
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
  int currentChiplets = THREAD_SIZE / CORES_PER_CHIPLET;
  int current_chiplet;
  int spread_rate = 1;    
  int current_slot;
  std::vector<int> taskAssignmentsOnChips;

  bool steal_from_neighbors();
  bool attempt_steal_from_chiplet();
  void attempt_steal_from_other_chiplets();
 
private:
  size_t rank;
  bool stop;
  std::thread t;
  Coro_Scheduler * csched;
  TaskQueue taskQ;
  
  std::mutex l_mtx, r_mtx;
  std::vector<CharmWorker*>& all_workers;
};


class CharmScheduler : public SchedulerImpl {
public:
  CharmScheduler(int worker_num);
  
  void start() override { start_barrier->wait(); }
  void finish() override;
  void await() override;
  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0) override;
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0) override;
  
  std::vector<WorkerImpl*> get_workers_impl() override;
  
  void start_perf_counters();

  /* helper interno */
  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

  PerfCounter* schedulerCounter;

private:
  size_t num_threads;
  std::vector<CharmWorker*> workers;
  Thread_Barrier * start_barrier;
};

} // namespace Charm
#endif