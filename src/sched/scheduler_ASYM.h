/* scheduler_ASYM.h */
#ifndef _SCHEDULER_ASYM_H_
#define _SCHEDULER_ASYM_H_

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

#include "scheduler_impl.h" // INTERFACCIA BASE
#include "sched/coroutine.h"
#include "sched/thread_barrier.h"
#include "tasking/task_queue.h"
#include "utils/timestamp.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "comm/mpienv.h"
#include "perf/perf_counter.h"

#define CORES_PER_CHIPLET_ASYM 8
#define CORES_PER_NUMA_NODE_ASYM 64
#define NUMA_DOMAINS_ASYM 2
#define SCHEDULER_TIMER_ASYM 500 

namespace Charm {

// Forward decl
class AsymScheduler;

struct ThreadComputeData {
  double* buffer;
  size_t   size; 
};

struct NodeInfo {
  int nodeId;
};

struct ThreadInfoEx {
  int       threadId;    
  int       appId;       
  pid_t     tid;         
  std::thread::native_handle_type pthreadHandle;
  int       assignedNode;  
  int       assignedCore;  
  ThreadComputeData computeData; 
};

struct Application {
  int    appId;
  double Atm;   
  double Att;   
  double oldA;  
  std::unordered_map<int, double> Amm; 
  void* memPtr;
  size_t memSize;

  Application() 
    : appId(0), Atm(0.0), Att(0.0), oldA(0.0), memPtr(nullptr), memSize(0) 
  {}
};

struct Cluster {
  std::vector<ThreadInfoEx*> threads;
  double Crbw;    
  double Cweight; 
};

struct Placement {
  std::unordered_map<const Cluster*, int> clusterToNode; 
  double Pwbw; 
  double Pmm;  
};

// --- WORKER IMPLEMENTATION ---
class AsymWorker : public WorkerImpl {
public:
  AsymWorker(size_t ith, 
             int worker_num,
             Thread_Barrier * tb,
             std::vector<AsymWorker*>& all_workers,
             std::unordered_map<int, std::vector<int>> nodeToCores,
             std::vector<ThreadInfoEx> allThreads,
             AsymScheduler* scheduler_ptr); // Added ptr to parent

  void task_worker();
  void set_thread_affinity(int core_id);

  // Override metodi interfaccia
  void yield() override; // Qui andrà la logica complessa
  void maybe_yield() override { csched->coroutine_maybe_yield(); }
  void idle() override { csched->coroutine_idle(); }
  void wait(Condition* c) override { csched->coroutine_wait(c); }
  void signal(Condition* c) override { csched->coroutine_signal(c); }
  void spawn_coroutine(std::function<void()> f, int tag = 0) override { csched->coroutine_create(f, tag); }
  void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) override { csched->coroutine_create_periodic(f, tag); }
  void await() override { t.join(); }
  void finish() override { this->stop = true; }
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

  // Fields specific to ASYM
  PerfCounter* eventsCounter;
  std::chrono::steady_clock::time_point time;
  int currentChiplets = THREAD_SIZE / CORES_PER_CHIPLET_ASYM;
  int current_chiplet;
  int spread_rate = 1;    
  int current_slot;
  std::vector<int> taskAssignmentsOnChips;
  
  // Metodi specifici ASYM
  void update_location();
  void increase_spread();
  void decrease_spread();
  int calculateCore(int core);
  void spread();
  void compact();
  void assignBalanced();

private:
  size_t rank;
  bool stop;
  std::thread t;
  Coro_Scheduler * csched;
  TaskQueue taskQ;
  std::mutex l_mtx, r_mtx;
  std::vector<AsymWorker*>& all_workers;
  std::unordered_map<int, std::vector<int>> nodeToCores;
  std::vector<ThreadInfoEx> allThreads;
  AsymScheduler* parent_scheduler; // Accesso ai dati globali
};

// --- SCHEDULER IMPLEMENTATION ---
class AsymScheduler : public SchedulerImpl {
public:
  AsymScheduler(int worker_num);
  
  void start() override { start_barrier->wait(); }
  void finish() override;
  void await() override;
  void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0) override;
  void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0) override;
  std::vector<WorkerImpl*> get_workers_impl() override;

  void start_perf_counters();
  
  template<typename F>
  inline void private_enqueue(size_t ith, F f){
    workers[ith]->private_enqueue(f);
  }

  // Dati pubblici per l'algoritmo ASYM (accessibili dal Worker)
  PerfCounter* schedulerCounter;
  std::vector<NodeInfo> nodes;            
  std::vector<ThreadInfoEx> allThreads;     
  std::vector<Application> allApps;         
  std::unordered_map<int, std::vector<int>> nodeToCores;

private:
  size_t num_threads;
  std::vector<AsymWorker*> workers;
  Thread_Barrier * start_barrier;
};

// --- GLOBAL HELPER FUNCTIONS ---

inline void* allocateOnNode(size_t size, int node) {
  void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) return nullptr;
  struct bitmask* nodemask = numa_allocate_nodemask();
  numa_bitmask_clearall(nodemask);
  numa_bitmask_setbit(nodemask, node);
  if (mbind(ptr, size, MPOL_BIND, nodemask->maskp, nodemask->size, 0) != 0) {
      std::cerr << "[Error] mbind failed\n";
  }
  numa_free_nodemask(nodemask);
  return ptr;
}

inline std::vector<Cluster> formClusters(AsymScheduler* sys) {
  std::unordered_map<int, Cluster*> appClusterMap;
  std::vector<Cluster> clusters;
  for (auto &th : sys->allThreads) {
      if (appClusterMap.find(th.appId) == appClusterMap.end()) {
          Cluster c;
          c.threads.push_back(&th);
          c.Crbw = 0.0; 
          c.Cweight = 1.0;
          clusters.push_back(c);
          appClusterMap[th.appId] = &clusters.back();
      } else {
          appClusterMap[th.appId]->threads.push_back(&th);
      }
  }
  return clusters;
}

inline std::vector<Placement> computePlacements(const std::vector<Cluster> &clusters, const std::vector<NodeInfo> &nodes) {
  std::vector<Placement> placements;
  std::vector<int> mapping(clusters.size(), 0);
  std::function<void(int)> backtrack = [&](int idx) {
      if (idx == (int)clusters.size()) {
          Placement p;
          for (size_t i = 0; i < clusters.size(); i++) p.clusterToNode[(Cluster*)&clusters[i]] = mapping[i];
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

inline double computeClusterBandwidth(const Placement &pl, const std::vector<Cluster> &clusters) {
  double total = 0.0;
  for (auto &c : clusters) total += c.Crbw * c.Cweight;
  return total;
}

inline double computeMemoryMigrationCost(const Placement &pl, std::vector<Application> &apps) {
  double cost = 0.0;
  for (auto &app : apps) cost += (0.05 * app.Att);
  return cost;
}

inline bool setThreadAffinity(std::thread::native_handle_type thread, int cpuCore) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuCore, &cpuset);
    return (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0);
}

inline bool clusterBelongsToApp(const Cluster &cl, int appId) {
    for (auto *th : cl.threads) if (th->appId == appId) return true;
    return false;
}

inline void identifyMigratedApplications(const Placement &pl, const std::vector<Cluster> &clusters, std::vector<Application*> &migrated, AsymScheduler* sys) {
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

inline bool migrateMemoryPages(void* addr, size_t len, int targetNode) {
  long pageSize = sysconf(_SC_PAGESIZE);
  long numPages = (len + pageSize - 1) / pageSize;
  std::vector<void*> pages(numPages);
  for (long i = 0; i < numPages; i++) pages[i] = static_cast<char*>(addr) + i * pageSize;
  std::vector<int> nodes(numPages, targetNode);
  std::vector<int> status(numPages, -1);
  int ret = move_pages(0, numPages, pages.data(), nodes.data(), status.data(), 0);
  if (ret != 0) return false;
  return true;
}

inline void performThreadMigration(const Placement &pl, const std::vector<Cluster> &clusters, AsymScheduler* sys) {
  for (auto &cl : clusters) {
      int newNode = pl.clusterToNode.at((Cluster*)&cl);
      int coreToUse = sys->nodeToCores[newNode][0];
      for (auto *th : cl.threads) {
          if (setThreadAffinity(th->pthreadHandle, coreToUse)) {
             th->assignedNode = newNode;
             th->assignedCore = coreToUse;
          }
      }
  }
}

inline void dynamicMemoryMigration(const Placement &pl, const std::vector<Cluster> &clusters, AsymScheduler* sys) {
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
        if (!migrateMemoryPages(app.memPtr, app.memSize, targetNode)) {
              std::cerr << "[AsymSched] Memory migration failed for app " << app.appId << "\n";
              //std::cout << "[AsymSched] Migrated memory for app " << app.appId << " to node " << targetNode << "\n";
        }
      }
  }
}

inline void fullyMigrateMemory(Application &app) {
  if (app.memPtr && app.memSize > 0) {
      if (migrateMemoryPages(app.memPtr, app.memSize, 0)) {
          std::cout << "[AsymSched] Fully migrated memory\n";
          app.oldA = 0.0;
      }
  }
}

inline long long measureCacheMisses(pid_t tid, int cpu, int duration_ms = 100) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_HW_CACHE_MISSES; 
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;

  int fd = static_cast<int>(syscall(__NR_perf_event_open, &pe, getpid(), -1, -1, 0));
  if (fd == -1) return -1;
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  long long count = 0;
  if (read(fd, &count, sizeof(count)) == -1) count = 0;
  close(fd);
  return count;
}

} // namespace Charm
#endif