/* scheduler.h
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


#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <thread>
#include <functional>
#include <mutex>
#include <vector>
#include <queue>
#include <iostream>
#include <future>
#include <chrono>
#include <numaif.h>

#include "sched/coroutine.h"
#include "sched/thread_barrier.h"
#include "tasking/task_queue.h"
#include "utils/timestamp.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "comm/mpienv.h"
#include "perf/perf_counter.h"

#define CHIPLETS 8
#define CORES_PER_CHIPLET 8
// #define RMT_CHIP_ACCESS_RATE 1000
#define RMT_CHIP_ACCESS_RATE 300
#define CORES_PER_NUMA_NODE 64

#define NUMA_DOMAINS 2
#define TOTAL_CORES 128

#define TOTAL_CHIPLETS 8
#define CORES_PER_CHIPLET 8

// #define SCHEDULER_TIMER 500 // Good for 20
#define SCHEDULER_TIMER 750 // Good for all with 64 cores with bfs
// #define SCHEDULER_TIMER 200 // 

namespace Charm{

extern bool global_exit_flag;
extern thread_local bool allow_yield;

class Worker{
public:

  Worker( size_t ith, 
          int worker_num,
          Thread_Barrier * tb,
          std::vector<Worker*>& all_workers);

  void task_worker();

  void set_thread_affinity(int core_id);

  void update_location() {
    if (spread_rate <= 0 || spread_rate > CHIPLETS || THREAD_SIZE >= spread_rate*CORES_PER_CHIPLET) return; // Bounds checking
    current_chiplet = (rank / (CORES_PER_CHIPLET / spread_rate));
    current_slot = rank % (CORES_PER_CHIPLET/spread_rate);
    if (current_chiplet >= CHIPLETS) {
      current_chiplet = current_chiplet  % CHIPLETS;
      current_slot = current_slot + rank / CORES_PER_CHIPLET;
    }
    int core = current_chiplet * CHIPLETS + current_slot;
    set_thread_affinity(core);
    int numa_node = core / CORES_PER_NUMA_NODE;
    numa_set_preferred(numa_node);
  }

  // Increase the spread of cores among chiplets
  void increase_spread() {
    if (spread_rate < CHIPLETS) {
        spread_rate = spread_rate + 1;
        update_location();
    }
  }

  // Decrease the spread of cores among chiplets
  void decrease_spread() {
    if (spread_rate > 1) {
        spread_rate = spread_rate -1;
        update_location();
    }
  }

  int calculateCore(int core) {
    int base = (core % CHIPLETS) * CORES_PER_CHIPLET;  // This calculates the multiple of 8 part
    int cycle = core / CHIPLETS;       // This determines whether to add 0 or 1
    
    if (cycle % 2 == 0) {
        return base;
    } else {
        return base + 1;
    }
}

// ------------- NEW SCHEDULER --------------

void spread()
{
  if (currentChiplets + 1 <= TOTAL_CHIPLETS) {
    currentChiplets = currentChiplets + 1;
    assignBalanced();
  }

}

void compact()
{
  if (currentChiplets - 1 > 0) {
    currentChiplets = currentChiplets - 1;
    assignBalanced();
  }
}

void assignBalanced()
{
  int tasksPerChiplet = THREAD_SIZE / currentChiplets;
  int extraTasks = THREAD_SIZE % currentChiplets;

  int taskIndex = 0;
  for (int chiplet = 0; chiplet < currentChiplets; ++chiplet)
  {
    int chipletsTaskCount = tasksPerChiplet + (chiplet < extraTasks ? 1 : 0);
    for (int i = 0; i < chipletsTaskCount; ++i)
    {
      taskAssignmentsOnChips[taskIndex] = chiplet * CORES_PER_CHIPLET + i;
      taskIndex++;
    }
  }
  int core = taskAssignmentsOnChips[rank];
  set_thread_affinity(core);
}

// void assignBalanced_old()
// {
//   int tasksPerChiplet = THREAD_SIZE / currentChiplets;
//   int extraTasks = THREAD_SIZE % currentChiplets;

//   // Calculate which chiplet this rank belongs to
//   int chiplet = rank / tasksPerChiplet;
//   if (chiplet >= currentChiplets) chiplet = currentChiplets - 1;

//   // Calculate the offset within the chiplet
//   int offsetInChiplet = rank - (chiplet * tasksPerChiplet);
//   if (offsetInChiplet >= tasksPerChiplet + (chiplet < extraTasks ? 1 : 0)) {
//     offsetInChiplet = tasksPerChiplet + (chiplet < extraTasks ? 1 : 0) - 1;
//   }

//   // Calculate the core for this rank
//   int core = chiplet * CORES_PER_CHIPLET + offsetInChiplet;

//   // Set thread affinity
//   set_thread_affinity(core);

//   // Set NUMA policy
//   int numa_node = core / CORES_PER_NUMA_NODE;
//   unsigned long nodemask = 1UL << numa_node;
//   if (set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8) == -1) {
//     std::cout << "set_mempolicy error" << std::endl;
//   }
// }

// void updateCurrentChiplets()
// {
//   std::set<int> usedChiplets;
//   for (int assignment : taskAssignments)
//   {
//     usedChiplets.insert(assignment / CORES_PER_CHIPLET);
//   }
//   currentChiplets = usedChiplets.size();
// }

// -----------------------------------------



  inline void yield(){

    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
    
    if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {
      uint64_t counter = eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
      uint64_t result = counter / elapsed_time.count()*SCHEDULER_TIMER;
      //std::cout << "SET THRTEAD AFFINITY: " << result << " eventCounter = " << counter << " ed elapsed_time = " << elapsed_time.count() << std::endl;
      if (result >= RMT_CHIP_ACCESS_RATE) {
        // Spread the workers into more chiplets
        increase_spread();
      } else {
        if (result > 0) {
         // Concentrate the workers in less chiplets
          decrease_spread();
        }
      }
      time = std::chrono::steady_clock::now();
      eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
    }

    csched->coroutine_yield();
  }


  inline void maybe_yield(){
    csched->coroutine_maybe_yield();
  }

  inline void idle(){
    csched->coroutine_idle();
  }

  inline void wait(Condition* c){
    csched->coroutine_wait(c); 
  }

  inline void signal(Condition* c){
    csched->coroutine_signal(c);
  }

  inline void spawn_coroutine(std::function<void()> f, int tag = 0){
    csched->coroutine_create(f, tag);
  }

  inline void spawn_coroutine_periodic(std::function<void()> f, int tag = 0){
    csched->coroutine_create_periodic(f, tag);
  }

  inline void await(){
    t.join();
  }

  inline void finish(){
    this->stop = true;
    eventsCounter->stopCounters();
  }

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
      //signal(this->have_tasks);
    }else{
      task_created_cnt ++;
      uint64_t args[3] = {0};
      char* copyf = reinterpret_cast<char*>(&f);
      char* fargs = reinterpret_cast<char*>(&args);
      memcpy(fargs, copyf, sizeof(f));
      //uint64_t * args = reinterpret_cast<uint64_t*>(&f);
      taskQ.spawn_private(task_functor_proxy<F>, args[0],args[1],args[2]); 
      //signal(this->have_tasks);
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
  std::vector<Worker*>& all_workers;
};


class Scheduler{
public:

  Scheduler(int worker_num);
  void await();
  void finish();
  size_t get_size();
  int get_id();
  inline Worker* get_cur_worker(){
    return workers[get_id()];
  }

  inline Worker* get_worker(size_t index) {
        return workers[index];
    }

  inline void start(){
    start_barrier->wait();
  }

  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0);
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0);

  void start_perf_counters();
  std::vector<Worker*>& get_workers() { return workers; }


  /* enqueue a private task */
  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

  PerfCounter* schedulerCounter;
  

private:
  size_t num_threads;
  std::vector<Worker*> workers;
  Thread_Barrier * start_barrier;
  // std::vector<Worker*> workers;
};

extern Scheduler * global_scheduler;

void yield();
void maybe_yield();

inline size_t thread_rank(){
  return global_scheduler->get_id();
}

inline size_t thread_size(){
  return global_scheduler->get_size();
}

inline void set_allow_yield(bool val){
  allow_yield = val;
}

inline size_t my_id(){
  return mpi_env->rank * THREAD_SIZE + thread_rank();
}

template <typename F>
inline void delivery_private(F f){
   global_scheduler->private_enqueue(thread_rank(), f);
}

}//namespace Charm
#endif
