/* sched/scheduler.h */
#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <vector>
#include <thread>
#include <functional>
#include <mutex>
#include <atomic>
#include <iostream>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <sys/syscall.h>
#include <unordered_map>

#ifdef NUMA_AWARE
#include <numa.h>
#include <numaif.h>
#endif

#include "sched/coroutine.h"
#include "tasking/task_queue.h"
#include "sched/thread_barrier.h"
#include "perf/perf_counter.h"
#include "comm/mpienv.h"
#include "utils/memlog.h" 

// --- Global Configuration ---
#define CHIPLETS 8
#define CORES_PER_CHIPLET 8
#define CORES_PER_NUMA_NODE 64
#define NUMA_DOMAINS 2
#define TOTAL_CORES 128
#define SCHEDULER_TIMER 750 

namespace Charm {

class Scheduler;
extern Scheduler * global_scheduler;
extern bool global_exit_flag;
extern thread_local int thread_id;
extern thread_local bool allow_yield;

// --------------------------------------------------------
// BASE WORKER CLASS
// --------------------------------------------------------
class Worker {
public:
    Worker(size_t ith, int worker_num, Thread_Barrier* tb);
    virtual ~Worker();

    virtual void yield() = 0; 
    virtual void maybe_yield();
    virtual void task_worker(); 
    virtual void idle();
    
    // Helpers needed by other parts of Charm
    void wait(Condition* c);
    void signal(Condition* c);
    void add_task_worker(int num);

    // Common API
    void spawn_coroutine(std::function<void()> f, int tag = 0);
    void spawn_coroutine_periodic(std::function<void()> f, int tag = 0);
    void set_thread_affinity(int core_id);
    void finish();
    void await();

    template<typename F>
    void private_enqueue(F f){
        if( sizeof(f) > sizeof(arg_pack_t) ){
            heap_task_created_cnt ++;
            F * tp = new F(f);
            taskQ.spawn_private(task_heapfunctor_proxy<F>, tp, tp, tp);
        } else {
            task_created_cnt ++;
            uint64_t args[3] = {0};
            char* copyf = reinterpret_cast<char*>(&f);
            char* fargs = reinterpret_cast<char*>(&args);
            memcpy(fargs, copyf, sizeof(f));
            taskQ.spawn_private(task_functor_proxy<F>, args[0],args[1],args[2]); 
        }
    }

    size_t rank;
    PerfCounter* eventsCounter = nullptr;
    
    // Stats
    uint64_t heap_task_created_cnt = 0;
    uint64_t task_created_cnt = 0;

protected:
    std::thread t;
    Coro_Scheduler* csched;
    TaskQueue taskQ;
    Thread_Barrier* barrier;
    bool stop = false;
    int worker_num_init;

    void worker_entry();
};

// --------------------------------------------------------
// BASE SCHEDULER CLASS
// --------------------------------------------------------
class Scheduler {
public:
    Scheduler(int num_threads);
    virtual ~Scheduler();

    static Scheduler* create(std::string type, int worker_num);

    virtual void start_perf_counters();
    virtual void finish();
    virtual void await();

    // Needed by Charm.cc
    void start() { start_barrier->wait(); }
    
    Worker* get_cur_worker();
    Worker* get_worker(size_t index);
    size_t get_size() { return num_threads; }
    int get_id();

    void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0);
    void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0);

    template <typename F>
    void private_enqueue(size_t ith, F f) {
        workers[ith]->private_enqueue(f);
    }

protected:
    size_t num_threads;
    std::vector<Worker*> workers; 
    Thread_Barrier* start_barrier;
};

// Global Helpers
inline size_t thread_rank() { return global_scheduler->get_id(); }
inline size_t thread_size() { return global_scheduler->get_size(); }
void yield(); 
void maybe_yield();

inline size_t my_id(){
  return mpi_env->rank * THREAD_SIZE + thread_rank();
}

template <typename F>
inline void delivery_private(F f){
   global_scheduler->private_enqueue(thread_rank(), f);
}

// Needed by communicator.cc
inline void set_allow_yield(bool val){
  allow_yield = val;
}

} // namespace Charm

#endif