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

#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cerrno>

#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <numa.h>

#include <linux/perf_event.h>

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
#define RMT_CHIP_ACCESS_RATE 300
// #define RMT_CHIP_ACCESS_RATE 500
#define CORES_PER_NUMA_NODE 64

#define NUMA_DOMAINS 2
#define TOTAL_CORES 128
// #if THREAD_SIZE > 64
//     #define TOTAL_CHIPLETS 16
// #else
//     #define TOTAL_CHIPLETS 8
// #endif


#define TOTAL_CHIPLETS 8
#define CORES_PER_CHIPLET 8
// #define TASKS THREAD_SIZE THREAD_SIZE
// #define SCHEDULER_TIMER 1000 // Good for 20
#define SCHEDULER_TIMER 750 // Good for all with 64 cores with bfs
// #define SCHEDULER_TIMER 200 // 

namespace Charm{

struct ThreadComputeData {
  double* buffer;
  size_t   size; // number of doubles
};

struct NodeInfo {
  int nodeId;
};

struct ThreadInfoEx {
  int       threadId;    // Logical ID (0 .. 63)
  int       appId;       // Application ID (e.g. 100 or 200)
  pid_t     tid;         // OS thread ID (as obtained via SYS_gettid)
  std::thread::native_handle_type pthreadHandle;
  int       assignedNode;  // NUMA node ID currently assigned
  int       assignedCore;  // CPU core assigned
  ThreadComputeData computeData; // Pointer to compute buffer and size
};

struct Application {
  int    appId;
  double Atm;   // Overhead/time for thread migration (in arbitrary units)
  double Att;   // Total performance budget (arbitrary units)
  double oldA;  // Fraction of memory still on old node (0.0 to 1.0)
  std::unordered_map<int, double> Amm; // Migration overhead per node
  // For memory migration, each app has a large buffer.
  void*  memPtr;
  size_t memSize;

  Application() 
    : appId(0), Atm(0.0), Att(0.0), oldA(0.0), memPtr(nullptr), memSize(0) 
  {}

};

struct Cluster {
  std::vector<ThreadInfoEx*> threads;
  double Crbw;    // Aggregated measured memory bandwidth metric
  double Cweight; // Weight factor (set to 1.0 normally)
};

struct Placement {
  std::unordered_map<const Cluster*, int> clusterToNode; // Proposed mapping (cluster->node)
  double Pwbw; // Weighted bandwidth (sum over clusters)
  double Pmm;  // Estimated memory migration overhead
};

extern bool global_exit_flag;
extern thread_local bool allow_yield;

class Worker{
public:

  Worker( size_t ith, 
          int worker_num,
          Thread_Barrier * tb,
          std::vector<Worker*>& all_workers,
          std::unordered_map<int, std::vector<int>> nodeToCores,
          std::vector<ThreadInfoEx> allThreads);

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
    unsigned long nodemask = 1UL << numa_node%NUMA_DOMAINS;
    if (set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8) == -1) {
        std::cout << "set_mempolicy error" << std::endl;
    }
    
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
  // std::cout << "Task index " << taskIndex << " to core " << core << std::endl;
  set_thread_affinity(core);
  int numa_node = core / CORES_PER_NUMA_NODE;
  unsigned long nodemask = 1UL << numa_node;
  if (set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8) == -1) {
    std::cout << "set_mempolicy error" << std::endl;
  }
}



// inline void yield(){

//     auto current_time = std::chrono::steady_clock::now();
//     auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
    
//     if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {
//       uint64_t counter = eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
//       uint64_t result = counter / elapsed_time.count()*SCHEDULER_TIMER;
//       //std::cout << "SET THRTEAD AFFINITY: " << result << " eventCounter = " << counter << " ed elapsed_time = " << elapsed_time.count() << std::endl;
//       if (result >= RMT_CHIP_ACCESS_RATE) {
//         // Spread the workers into more chiplets
//         increase_spread();
//         // spread();
//         // if (spread_rate < CHIPLETS) {
//         //   spread_rate += 1;
//         //   update_location();
//         // }
//       } else {
//         if (result > 0) {
//          // std::cout << "Result = " << result << std::endl;
//          // Concentrate the workers in less chiplets
//           decrease_spread();
//           // compact();
//           // if (spread_rate > 1) {
//           //   spread_rate = spread_rate -1;
//           //   update_location();
//           // }
//         }
//       }
//       time = std::chrono::steady_clock::now();
//       eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
//     }

//     csched->coroutine_yield();
//   }

  // ---------------------------------------------------------------
  // OLD yield

  inline void yield(){

      // auto &sys = SystemState::instance();
      // for (auto &th : sys.allThreads) {
      //     while (th.tid == 0) {
      //         std::this_thread::sleep_for(std::chrono::milliseconds(5));
      //     }
      // }

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
    // eventsCounter->stopCounters();

    //std::cout << "Worker: " << rank << "\n" << std::endl;
    //eventsCounter->printReportByLine(std::cout, 1);
    //std::cout << std::endl;
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
  //Condition * have_tasks;
  TaskQueue taskQ;
  
  std::mutex l_mtx, r_mtx;
  // int current_chiplet;
  std::vector<Worker*>& all_workers;
  std::unordered_map<int, std::vector<int>> nodeToCores;
  std::vector<ThreadInfoEx> allThreads;
};


// New version: takes ThreadInfoEx* as argument.
inline void* computeThreadFunc(void* arg) {
  ThreadInfoEx* threadInfo = static_cast<ThreadInfoEx*>(arg);
  // Record the actual OS thread id.
  threadInfo->tid = syscall(SYS_gettid);
  ThreadComputeData* data = &threadInfo->computeData;
  
  // Now run your compute loop.
  volatile double sum = 0.0;
  while (true) {
      for (size_t i = 0; i < data->size; i++) {
          data->buffer[i] = data->buffer[i] * 1.001 + 0.0001;
          sum += data->buffer[i];
      }
      std::this_thread::yield();
  }
  return nullptr;
}

inline long long measureCacheMisses(pid_t tid, int cpu, int duration_ms = 100) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_HW_CACHE_MISSES;  // Use cache-misses as a proxy
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;


  int fd = static_cast<int>(syscall(__NR_perf_event_open, &pe, getpid(), -1, -1, 0));
  if (fd == -1) {
      std::cerr << "[Error] perf_event_open failed: " << strerror(errno) << "\n";
      return -1;
  }
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

  // std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));

  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  long long count = 0;
  if (read(fd, &count, sizeof(count)) == -1) {
      std::cerr << "[Error] read() on perf event failed: " << strerror(errno) << "\n";
  }
  close(fd);
  return count;
}

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
  std::unordered_map<int, std::vector<int>>& get_node_to_cores() { return nodeToCores; }

  /* enqueue a private task */
  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

  PerfCounter* schedulerCounter;
  std::vector<NodeInfo> nodes;            // NUMA nodes (4 expected)
  std::vector<ThreadInfoEx> allThreads;     // 64 threads in total
  std::vector<Application> allApps;         // Two applications
  // Map from node id to list of CPU cores (simulate 16 cores per node)
  std::unordered_map<int, std::vector<int>> nodeToCores;

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

// Allocate memory and bind it to a specific NUMA node using mbind.
inline void* allocateOnNode(size_t size, int node) {
  void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
      std::cerr << "[Error] mmap failed: " << strerror(errno) << "\n";
      return nullptr;
  }
  struct bitmask* nodemask = numa_allocate_nodemask();
  numa_bitmask_clearall(nodemask);
  numa_bitmask_setbit(nodemask, node);
  int mode = MPOL_BIND;
  if (mbind(ptr, size, mode, nodemask->maskp, nodemask->size, 0) != 0) {
      std::cerr << "[Error] mbind failed: " << strerror(errno) << "\n";
  }
  numa_free_nodemask(nodemask);
  return ptr;
}

// Form clusters by grouping threads with the same application ID.
inline std::vector<Cluster> formClusters(Scheduler* sys) {
  std::unordered_map<int, Cluster*> appClusterMap;
  std::vector<Cluster> clusters;
  for (auto &th : sys->allThreads) {
      if (appClusterMap.find(th.appId) == appClusterMap.end()) {
          Cluster c;
          c.threads.push_back(&th);
          c.Crbw = 0.0; // will be measured later
          c.Cweight = 1.0;
          clusters.push_back(c);
          appClusterMap[th.appId] = &clusters.back();
      } else {
          appClusterMap[th.appId]->threads.push_back(&th);
      }
  }
  return clusters;
}

// Enumerate candidate placements: map each cluster to one of the available nodes.
inline std::vector<Placement> computePlacements(const std::vector<Cluster> &clusters,
                                        const std::vector<NodeInfo> &nodes) {
  std::vector<Placement> placements;
  std::vector<int> mapping(clusters.size(), 0);
  std::function<void(int)> backtrack = [&](int idx) {
      if (idx == (int)clusters.size()) {
          Placement p;
          for (size_t i = 0; i < clusters.size(); i++) {
              p.clusterToNode[(Cluster*)&clusters[i]] = mapping[i];
          }
          placements.push_back(p);
          return;
      }
      for (int n = 0; n < (int)nodes.size(); n++) {
          mapping[idx] = n;
          backtrack(idx + 1);
      }
  };
  backtrack(0);
  return placements;
}

// Compute Pwbw for a placement as the sum over clusters: Crbw * Cweight.
inline double computeClusterBandwidth(const Placement &pl,
                              const std::vector<Cluster> &clusters) {
  double total = 0.0;
  for (auto &c : clusters) {
      total += c.Crbw * c.Cweight;
  }
  return total;
}

// Compute memory migration cost (Pmm) for a placement.
inline double computeMemoryMigrationCost(const Placement &pl,
                                std::vector<Application> &apps) {
  double cost = 0.0;
  // For each app, add a cost if its cluster is moved.
  for (auto &app : apps) {
      cost += (0.05 * app.Att);
  }
  return cost;
}

// Set thread CPU affinity to a given core.
inline bool setThreadAffinity(std::thread::native_handle_type thread, int cpuCore) {
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(cpuCore, &cpuset);
int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
if (ret != 0) {
    std::cerr << "[Error] pthread_setaffinity_np: " << strerror(errno) << "\n";
    return false;
}
return true;
}

inline bool clusterBelongsToApp(const Cluster &cl, int appId) {
for (auto *th : cl.threads) {
    if (th->appId == appId) return true;
}
return false;
}

// Identify migrated applications: if a cluster’s new node differs from its first thread’s old node.
inline void identifyMigratedApplications(const Placement &pl,
                                const std::vector<Cluster> &clusters,
                                std::vector<Application*> &migrated) {
  auto &sys = global_scheduler;
  for (auto &app : sys->allApps) {
      bool changed = false;
      for (auto &cl : clusters) {
          if (clusterBelongsToApp(cl, app.appId)) {
              int newNode = pl.clusterToNode.at(&cl);
              if (!cl.threads.empty() && newNode != cl.threads[0]->assignedNode) {
                  changed = true;
                  break;
              }
          }
      }
      if (changed) migrated.push_back(&app);
  }
}

// Use move_pages to migrate memory pages of a region to target NUMA node.
inline bool migrateMemoryPages(void* addr, size_t len, int targetNode) {
  long pageSize = sysconf(_SC_PAGESIZE);
  long numPages = (len + pageSize - 1) / pageSize;
  std::vector<void*> pages(numPages);
  for (long i = 0; i < numPages; i++) {
      pages[i] = static_cast<char*>(addr) + i * pageSize;
  }
  std::vector<int> nodes(numPages, targetNode);
  std::vector<int> status(numPages, -1);
  int ret = move_pages(0, numPages, pages.data(), nodes.data(), status.data(), 0);
  if (ret != 0) {
      std::cerr << "[Error] move_pages failed: " << strerror(errno) << "\n";
      return false;
  }
  for (int s : status) {
      if (s < 0) return false;
  }
  return true;
}

// Migrate threads by setting CPU affinity according to bestPlacement.
inline void performThreadMigration(const Placement &pl, const std::vector<Cluster> &clusters) {
  // auto &sys = SystemState::instance();
  auto &sys = global_scheduler;
  for (auto &cl : clusters) {
      int newNode = pl.clusterToNode.at((Cluster*)&cl);
      // Pick a CPU core from that node (first core in nodeToCores mapping).
      int coreToUse = sys->nodeToCores[newNode][0];
      for (auto *th : cl.threads) {
          if (!setThreadAffinity(th->pthreadHandle, coreToUse)) {
              std::cerr << "[AsymSched] Failed to set affinity for thread " << th->threadId << "\n";
          }
          th->assignedNode = newNode;
          th->assignedCore = coreToUse;
      }
  }
  // std::cout << "[AsymSched] Threads migrated via CPU affinity.\n";
}

// Migrate memory using move_pages for each application whose threads have moved.
inline void dynamicMemoryMigration(const Placement &pl, const std::vector<Cluster> &clusters) {
  // auto &sys = SystemState::instance();
  auto &sys = global_scheduler;
  for (auto &app : sys->allApps) {
      bool needMigration = false;
      int targetNode = -1;
      for (auto &cl : clusters) {
          if (clusterBelongsToApp(cl, app.appId)) {
              int newNode = pl.clusterToNode.at((Cluster*)&cl);
              if (!cl.threads.empty() && newNode != cl.threads[0]->assignedNode) {
                  needMigration = true;
                  targetNode = newNode;
                  break;
              }
          }
      }
      if (needMigration && app.memPtr && app.memSize > 0) {
        if (migrateMemoryPages(app.memPtr, app.memSize, targetNode)) {
              std::cout << "[AsymSched] Migrated memory for app " << app.appId
                        << " to node " << targetNode << "\n";
          } else {
              std::cerr << "[AsymSched] Memory migration failed for app " << app.appId << "\n";
          }
      }
  }
}

// Fully migrate memory (e.g. if >90% remains on old node) by re-binding pages.
inline void fullyMigrateMemory(Application &app) {
  if (app.memPtr && app.memSize > 0) {
      if (migrateMemoryPages(app.memPtr, app.memSize, 0)) {
          std::cout << "[AsymSched] Fully migrated memory for app " << app.appId << "\n";
          app.oldA = 0.0;
      }
  }
}


}//namespace Charm
#endif
