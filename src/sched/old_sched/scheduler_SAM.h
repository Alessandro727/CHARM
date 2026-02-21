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
#define SCHEDULER_TIMER 1000 // Good for all with 64 cores with bfs
// #define SCHEDULER_TIMER 200 // 

//<<< ADDED FOR AMD SUPPORT >>>
#include <cpuid.h>   // for __get_cpuid


// ------------------ Thresholds (from the paper) ------------------ //
static const double C_rT = 5.5e5;    // inter-socket coherence threshold (events/s)
static const double R_rT = 2.7e6;    // remote DRAM threshold (events/s)
static const double M_rT = 7.5e7;    // memory bandwidth threshold (LLC misses/s)


// ------------------ AMD/Intel Event Configs ------------------ //

// A small helper struct to bundle all the raw event codes we need:
struct RawEventCodes {
    uint64_t l2miss;
    uint64_t l3hit;
    uint64_t l3miss;
    uint64_t llcMiss;
    uint64_t remoteFwd;
    uint64_t remoteHitm;
    uint64_t remoteDram;
};

// We'll store our chosen event codes in a global. We'll fill it at runtime
// based on the detected CPU vendor.
static RawEventCodes gEvt;

// Detect CPU vendor by calling cpuid(0). Returns "GenuineIntel", "AuthenticAMD", etc.
static std::string detectCpuVendor()
{
    unsigned int eax, ebx, ecx, edx;
    char vendor[13];
    // cpuid with eax=0: returns highest eax and the vendor ID in ebx:edx:ecx
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';
    return std::string(vendor);
}

// ------------------ Data Structures ------------------ //
struct CoherenceMetrics {
    double l2miss;
    double l3hit;
    double l3miss;
    double llcMiss;
    double remoteFwd;
    double remoteHitm;
    double remoteDram;
    double intraSocket;  // = l2miss - (l3hit + l3miss)
    double interSocket;  // = remoteFwd + remoteHitm
};

struct TaskInfo {
    int id;
    std::thread pthreadHandle;
    std::atomic<pid_t> tid;  // OS TID
    int assignedSocket; // current socket [0..NUMA_DOMAINS-1]
    int assignedCore;   // core within socket
    int originalSocket; // initial socket
    // // Raw counters from Set 1:
    // double l2miss;
    // double l3hit;
    // double l3miss;
    // double llcMiss;
    // // Raw counters from Set 2:
    // double remoteFwd;
    // double remoteHitm;
    // double remoteDram;
    // // Derived metrics:
    // CoherenceMetrics metrics;
    // // Perf event file descriptors:
    // int fd_l2miss;
    // int fd_l3hit;
    // int fd_l3miss;
    // int fd_llcMiss;
    // int fd_remoteFwd;
    // int fd_remoteHitm;
    // int fd_remoteDram;


    PerfCounter::event l2miss;
    PerfCounter::event l3hit;
    PerfCounter::event l3miss;
    PerfCounter::event llcMiss;
    PerfCounter::event remoteFwd;
    PerfCounter::event remoteHitm;
    PerfCounter::event remoteDram;
    // Derived metrics:
    CoherenceMetrics metrics;
    double l2miss_value;
    double l3hit_value;
    double l3miss_value;
    double llcMiss_value;
    double remoteFwd_value;
    double remoteHitm_value;
    double remoteDram_value;
    // Worker memory buffer and its size (in number of doubles).
    double* buffer;
    size_t bufSize;
    std::chrono::steady_clock::time_point lastMigration;
};

static std::mutex tasksMutex;  // protects tasks

struct WorkerData {
    double* buffer;
    size_t size;
    int taskIndex; // index into tasks
};



// ------------------ Perf Event Functions ------------------ //

// If you truly want to fallback to "generic" events on AMD, you can do something
// like this helper that picks either raw or generic type/config:
// static int openPerfEvent(pid_t tid, uint64_t rawConfig, bool fallbackGenericLlcMiss=false)
// {
//     struct perf_event_attr pea;
//     memset(&pea, 0, sizeof(pea));
    
//     if (rawConfig != 0 && !fallbackGenericLlcMiss) {
//         // Use raw event as is (likely Intel or a known AMD raw code).
//         pea.type = PERF_TYPE_RAW;
//         pea.config = rawConfig;
//     }
//     else {
//         // Use a generic hardware event as a fallback example
//         // e.g. for L3 or LLC misses:
//         pea.type = PERF_TYPE_HARDWARE;
//         pea.config = PERF_COUNT_HW_CACHE_MISSES;
//     }
    
//     pea.size = sizeof(pea);
//     pea.disabled = 1;
//     pea.exclude_kernel = 1;
//     pea.exclude_hv = 1;
    
//     int fd = syscall(__NR_perf_event_open, &pea, tid, -1, -1, 0);
//     if (fd == -1) {
//         std::cerr << "[Error] perf_event_open failed for tid=" << tid
//                   << " config=0x" << std::hex << rawConfig << ": "
//                   << strerror(errno) << "\n";
//     }
//     return fd;
// }

static void openEvent(PerfCounter::event& e, uint64_t config, pid_t tid) {
  memset(&e.pe, 0, sizeof(e.pe));
  e.pe.type = PERF_TYPE_RAW;
  e.pe.size = sizeof(e.pe);
  e.pe.config = config;
  e.pe.disabled = 1;
  e.pe.exclude_kernel = 0;
  e.pe.exclude_hv = 1;
  e.pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                     PERF_FORMAT_TOTAL_TIME_RUNNING |
                     PERF_FORMAT_ID;

  e.fd = syscall(__NR_perf_event_open, &e.pe, tid, -1, -1, 0);
  if (e.fd == -1) {
      std::cerr << "[Perf] Failed to open event config=0x" 
                << std::hex << config << ": " << strerror(errno) << "\n";
  }
}

static void startEventSAM(PerfCounter::event& e) {
  if (e.fd < 0) return;
  ioctl(e.fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(e.fd, PERF_EVENT_IOC_ENABLE, 0);
  read(e.fd, &e.prev, sizeof(e.prev));
}

static double stopEventSAM(PerfCounter::event& e, double elapsedSec) {
  if (e.fd < 0) return 0.0;
  ioctl(e.fd, PERF_EVENT_IOC_DISABLE, 0);
  if (read(e.fd, &e.data, sizeof(e.data)) == -1) {
      std::cerr << "[Perf] Error reading event: " << strerror(errno) << "\n";
      return 0.0;
  }

  uint64_t delta = e.data.value - e.prev.value;
  uint64_t enabled = e.data.time_enabled - e.prev.time_enabled;
  uint64_t running = e.data.time_running - e.prev.time_running;

  double correction = (enabled > 0 && running > 0) ?
                      (double)enabled / running : 1.0;

  return ((double)delta * correction) / elapsedSec;
}


// static void startPerf(int fd)
// {
//     if (fd < 0) return;
//     ioctl(fd, PERF_EVENT_IOC_RESET, 0);
//     ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
// }

// static double stopPerf(int fd, double elapsedSec)
// {
//   if (fd < 0) return 0.0;
//   std::cerr << "Disabling event...\n";
//   ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
//   std::cerr << "Reading event...\n";
//   long long count = 0;
//   ssize_t res = read(fd, &count, sizeof(count));
//   if (res == -1) {
//       std::cerr << "[Error] read perf: " << strerror(errno) << "\n";
//       return 0.0;
//   }
//   std::cerr << "Read bytes: " << res << ", count: " << count << "\n";
//   return (double)count / elapsedSec;
// }

// ------------------ Thread Affinity and NUMA Binding ------------------ //
// static bool setThreadAffinity(pthread_t thr, int globalCore)
// {
//     cpu_set_t cpuset;
//     CPU_ZERO(&cpuset);
//     CPU_SET(globalCore, &cpuset);
//     int ret = pthread_setaffinity_np(thr, sizeof(cpuset), &cpuset);
//     if (ret != 0) {
//         std::cerr << "[Error] setThreadAffinity: " << strerror(errno) << "\n";
//         return false;
//     }
//     return true;
// }

static bool setThreadAffinity(int core_id) {

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
      std::cerr << "Error setting thread affinity: " << rc << std::endl;
  }
  // int numa_node = core_id / CORES_PER_NUMA_NODE;
  // numa_set_preferred(numa_node);
      // int numa_node = core_id / CORES_PER_NUMA_NODE;
  // unsigned long nodemask = 1UL << numa_node;
  // if (set_mempolicy(MPOL_BIND, &nodemask, sizeof(nodemask) * 8) == -1) {
  //     std::cout << "set_mempolicy error" << std::endl;
  // }
  // struct bitmask* nodemask = numa_allocate_nodemask();
  // numa_bitmask_setbit(nodemask, numa_node);
  // numa_set_membind(nodemask);
  // numa_free_nodemask(nodemask);
  return true;
}

static int globalCoreId(int socketId, int coreInSocket)
{
    return socketId * CORES_PER_NUMA_NODE + coreInSocket;
}

static bool bindMemoryToNode(void* addr, size_t len, int node)
{
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_clearall(nodemask);
    numa_bitmask_setbit(nodemask, node);
    numa_set_membind(nodemask);
    //int ret = mbind(addr, len, MPOL_BIND, nodemask->maskp, numa_max_node() + 1, 0);
    numa_free_nodemask(nodemask);
    //if (ret != 0) {
    //    std::cerr << "[Error] mbind failed: " << strerror(errno) << "\n";
    //    return false;
    //}
    return true;
}

static void migrateTask(TaskInfo &t, int targetSocket, int targetCoreInSocket)
{
    int globalCore = globalCoreId(targetSocket, targetCoreInSocket);
    if (setThreadAffinity(globalCore)) {
        t.assignedSocket = targetSocket;
        t.assignedCore = targetCoreInSocket;
        if (!bindMemoryToNode(t.buffer, t.bufSize * sizeof(double), targetSocket)) {
            std::cerr << "[Warning] Memory binding failed for task " << t.id << "\n";
        }
    }
}


// ------------------ Derived Metrics Calculation ------------------ //
static void deriveMetrics(TaskInfo &t)
{
    CoherenceMetrics &m = t.metrics;
    m.l2miss   = t.l2miss_value;
    m.l3hit    = t.l3hit_value;
    m.l3miss   = t.l3miss_value;
    m.llcMiss  = t.llcMiss_value;
    m.remoteFwd  = t.remoteFwd_value;
    m.remoteHitm = t.remoteHitm_value;
    m.remoteDram = t.remoteDram_value;
    m.intraSocket = m.l2miss - (m.l3hit + m.l3miss);
    m.interSocket = m.remoteFwd + m.remoteHitm;
}


namespace Charm{

extern bool global_exit_flag;
extern thread_local bool allow_yield;

class Worker{
public:

  Worker( size_t ith, 
          int worker_num,
          Thread_Barrier * tb,
          std::vector<Worker*>& all_workers,
          std::vector<std::unique_ptr<TaskInfo>>& tasks);

  void task_worker();

  void set_thread_affinity(int core_id);



// ------------- NEW SCHEDULER --------------



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



  // inline void yield(){

  //   auto current_time = std::chrono::steady_clock::now();
  //   auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
    
  //   if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {
  //     uint64_t counter = eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
  //     uint64_t result = counter / elapsed_time.count()*SCHEDULER_TIMER;
  //     //std::cout << "SET THRTEAD AFFINITY: " << result << " eventCounter = " << counter << " ed elapsed_time = " << elapsed_time.count() << std::endl;
  //     if (result >= RMT_CHIP_ACCESS_RATE) {
  //       // Spread the workers into more chiplets
  //       increase_spread();
  //       // spread();
  //       // if (spread_rate < CHIPLETS) {
  //       //   spread_rate += 1;
  //       //   update_location();
  //       // }
  //     } else {
  //       if (result > 0) {
  //        // std::cout << "Result = " << result << std::endl;
  //        // Concentrate the workers in less chiplets
  //         decrease_spread();
  //         // compact();
  //         // if (spread_rate > 1) {
  //         //   spread_rate = spread_rate -1;
  //         //   update_location();
  //         // }
  //       }
  //     }
  //     time = std::chrono::steady_clock::now();
  //     eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
  //   }

  //   csched->coroutine_yield();
  // }

  // ---------------------------------------------------------------
  // OLD yield

  

  inline void yield(){
    double samplingIntervalSec = 1.0;
    // std::cout << "YELD!" << std::endl;
  // for (auto &t : tasks) {
      
    // startEventSAM(ti->l2miss);
    // startEventSAM(ti->l3hit);
    // startEventSAM(ti->l3miss);
    // startEventSAM(ti->llcMiss);

    // // ... codice da profilare o sleep ...
    // sleep(1); // oppure misura tempo reale con chrono se preferisci

    // ti->l2miss_value  = stopEventSAM(ti->l2miss, samplingIntervalSec);
    // ti->l3hit_value   = stopEventSAM(ti->l3hit, samplingIntervalSec);
    // ti->l3miss_value  = stopEventSAM(ti->l3miss, samplingIntervalSec);
    // ti->llcMiss_value = stopEventSAM(ti->llcMiss, samplingIntervalSec);

    // startEventSAM(ti->remoteFwd);
    // startEventSAM(ti->remoteHitm);
    // startEventSAM(ti->remoteDram);

    // sleep(1);

    // ti->remoteFwd_value  = stopEventSAM(ti->remoteFwd, samplingIntervalSec);
    // ti->remoteHitm_value = stopEventSAM(ti->remoteHitm, samplingIntervalSec);
    // ti->remoteDram_value = stopEventSAM(ti->remoteDram, samplingIntervalSec);
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
    
    if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {

        // For INTEL !!!
	        //   // Event Code for OFFCORE_RESPONSE (typically 0xB7 for recent Intel CPUs)
			//   uint64_t offcore_event_code = 0xb7;
			//   // umask for OFFCORE_RESPONSE (typically 0x01 for any request type)
			//   uint64_t offcore_umask = 0x01;
	  
			//   // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE
			//   // Response from Local L3 Hit (Example Mask - VERIFY!)
			//   registerRawCounter("OFFCORE_RESPONSE:L3_HIT_LOCAL", offcore_event_code, offcore_umask, 0x3f803c0100); // L3 Hit M/E/S/F, LLC Hit, Local Source
	  
			//   // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL
			//   // Response from Remote Cache Hit (Same Socket) (Example Mask - VERIFY!)
			//    registerRawCounter("OFFCORE_RESPONSE:REMOTE_CACHE_HIT", offcore_event_code, offcore_umask, 0x3f803c0200); // L3 Hit M/E/S/F, LLC Hit, Remote Cache Source
	  
			//   // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT
			//   // Response from Remote Cache Hit (Different Socket) (Example Mask - VERIFY!)
			//   registerRawCounter("OFFCORE_RESPONSE:REMOTE_SOCKET_CACHE_HIT", offcore_event_code, offcore_umask, 0x3f803c0400); // L3 Hit M/E/S/F, LLC Hit, Remote Socket Source
	  
			//   // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL
			//   // Response from Local DRAM (Example Mask - VERIFY!)
			//   registerRawCounter("OFFCORE_RESPONSE:LOCAL_DRAM", offcore_event_code, offcore_umask, 0x3fbe048000); // LLC Miss, Local DRAM Source
	  
			//   // Equivalent to AMD: ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_RMT
			//   // Response from Remote DRAM (Example Mask - VERIFY!)
			//   registerRawCounter("OFFCORE_RESPONSE:REMOTE_DRAM", offcore_event_code, offcore_umask, 0x3fc2010000); // LLC Miss, Remote DRAM Source
    
        this->eventsCounter->resetCounter("MEM_LOAD_RETIRED:L2_MISS");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:L3_HIT_LOCAL");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:REMOTE_CACHE_HIT");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:REMOTE_SOCKET_CACHE_HIT");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:DEMAND_DATA_RD_REMOTE_CACHE_SNOOP_HITM");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:LOCAL_DRAM");
        this->eventsCounter->resetCounter("OFFCORE_RESPONSE:REMOTE_DRAM");

        // sleep(1);

        ti->l2miss_value = this->eventsCounter->getCounter("MEM_LOAD_RETIRED:L2_MISS");
        ti->l3hit_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:L3_HIT_LOCAL");
        ti->l3miss_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:REMOTE_CACHE_HIT");
        ti->llcMiss_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:REMOTE_SOCKET_CACHE_HIT");
        ti->remoteFwd_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:DEMAND_DATA_RD_REMOTE_CACHE_SNOOP_HITM");
        ti->remoteHitm_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:LOCAL_DRAM");
        ti->remoteDram_value = this->eventsCounter->getCounter("OFFCORE_RESPONSE:REMOTE_DRAM");



    // std::cout << "DONE EVENTS YELD!" << std::endl;
      // }
  // Derive metrics
  
      // std::lock_guard<std::mutex> lk(tasksMutex);
      // for (auto &t : tasks) {
          deriveMetrics(*ti);
      // }
      time = std::chrono::steady_clock::now();
      // eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
    }
  

  // // Run SAM scheduling
  //   runSAM(std::move(ti));
      
    
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
  std::unique_ptr<TaskInfo> ti;

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
  std::vector<std::unique_ptr<TaskInfo>> tasks;
  

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


static void runSAM(std::vector<std::unique_ptr<TaskInfo>> &tv)
{
    // std::lock_guard<std::mutex> lk(tasksMutex);
    std::vector<int> socketLoad(NUMA_DOMAINS, 0);
    for (auto &t : tv) {
        socketLoad[t->assignedSocket]++;
    }
    // Step 1: For tasks with high interSocket coherence
    std::vector<int> interTasks;
    for (size_t i = 0; i < tv.size(); i++) {
        if (tv[i]->metrics.interSocket > C_rT)
            interTasks.push_back((int)i);
    }
    if (!interTasks.empty()) {
        int bestSocket = 0, minLoad = socketLoad[0];
        for (int s = 1; s < NUMA_DOMAINS; s++) {
            if (socketLoad[s] < minLoad) {
                bestSocket = s;
                minLoad = socketLoad[s];
            }
        }
        for (int idx : interTasks) {
            if (tv[idx]->assignedSocket != bestSocket) {
                socketLoad[tv[idx]->assignedSocket]--;
                socketLoad[bestSocket]++;
                tv[idx]->assignedSocket = bestSocket;
            }
        }
    }
    // Step 2: If remoteDram > R_rT, revert to original socket
    for (auto &t : tv) {
        if (t->metrics.remoteDram > R_rT) {
            if (t->assignedSocket != t->originalSocket) {
                socketLoad[t->assignedSocket]--;
                socketLoad[t->originalSocket]++;
                t->assignedSocket = t->originalSocket;
            }
        }
    }
    // Step 3: If LLC_MISSES > M_rT, distribute
    for (auto &t : tv) {
        if (t->metrics.llcMiss > M_rT) {
            int bestSocket = t->assignedSocket;
            int minL = socketLoad[bestSocket];
            for (int s = 0; s < NUMA_DOMAINS; s++) {
                if (socketLoad[s] < minL) {
                    bestSocket = s;
                    minL = socketLoad[s];
                }
            }
            if (bestSocket != t->assignedSocket) {
                socketLoad[t->assignedSocket]--;
                socketLoad[bestSocket]++;
                t->assignedSocket = bestSocket;
            }
        }
    }
    // Step 4: Round-robin assignment of cores
    auto now = std::chrono::steady_clock::now();
    std::vector<int> coreIdx(NUMA_DOMAINS, 0);
    for (auto &t : tv) {
      // if (t->assignedCore == global_scheduler->get_cur_worker()->ti->assignedCore) {
        int targetCore = coreIdx[t->assignedSocket] % CORES_PER_NUMA_NODE;
        coreIdx[t->assignedSocket]++;
        if (t->assignedCore != targetCore) {
          auto duration = now - t->lastMigration;
          if (duration >= std::chrono::seconds(1) ) {
            t->lastMigration = now;
            migrateTask(*t, t->assignedSocket, targetCore);
          }
        }
      // }
    }

    // std::vector<int> coreIdx(NUMA_DOMAINS, 0);
    // for (auto &t : tv) {
    //     int targetCoreInSocket = coreIdx[t->assignedSocket] % CORES_PER_NUMA_NODE;
    //     coreIdx[t->assignedSocket]++;
    //     migrateTask(*t, t->assignedSocket, targetCoreInSocket);
    // }


}

}//namespace Charm
#endif
