/* scheduler_SAM.cc */
#include <future>
#include <thread>
#include <atomic>
#include <numaif.h>
#include <cmath>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h" 
#include "scheduler_SAM.h"

namespace Charm {

static const double C_rT = 5.5e5;    
static const double R_rT = 2.7e6;    
static const double M_rT = 7.5e7;    

static std::string detectCpuVendor() {
    unsigned int eax, ebx, ecx, edx;
    char vendor[13];
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    memcpy(vendor + 0, &ebx, 4);
    memcpy(vendor + 4, &edx, 4);
    memcpy(vendor + 8, &ecx, 4);
    vendor[12] = '\0';
    return std::string(vendor);
}

void SamWorker::set_thread_affinity(std::thread::native_handle_type t_handle, int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  int rc = pthread_setaffinity_np(t_handle, sizeof(cpu_set_t), &cpuset);
  if (rc != 0) std::cerr << "Error setting affinity for core " << core_id << ": " << rc << std::endl;
}

static bool bindMemoryToNode(void* addr, size_t len, int node) {
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_clearall(nodemask);
    numa_bitmask_setbit(nodemask, node);
    long ret = mbind(addr, len, MPOL_BIND, nodemask->maskp, nodemask->size, MPOL_MF_MOVE);
    numa_free_nodemask(nodemask);
    if (ret != 0) return false;
    return true;
}

static void migrateTask(TaskInfo &t, int targetSocket, int targetCoreInSocket) {
    int globalCore = targetSocket * CORES_PER_NUMA_NODE_SAM + targetCoreInSocket;
    
    SamWorker::set_thread_affinity(t.threadHandle, globalCore);
    
    t.assignedSocket = targetSocket;
    t.assignedCore = targetCoreInSocket;
    
    if (t.buffer) {
         bindMemoryToNode(t.buffer, t.bufSize * sizeof(double), targetSocket);
    }
}

static void deriveMetrics(TaskInfo &t) {
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

// --- SamWorker Implementation ---

SamWorker::SamWorker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<SamWorker*>& all_workers, SamScheduler* scheduler)
    : rank(ith), stop(false), all_workers(all_workers), parent_scheduler(scheduler) {
    
    this->time = std::chrono::steady_clock::now(); 

    std::unique_ptr<TaskInfo> newTask(new TaskInfo());
    newTask->id = ith;
    newTask->tid.store(0);
    newTask->assignedSocket = ith / CORES_PER_NUMA_NODE_SAM; 
    newTask->originalSocket = newTask->assignedSocket;
    newTask->assignedCore = ith % CORES_PER_NUMA_NODE_SAM;   
    
    newTask->l2miss_value = -1; newTask->l3hit_value = -1; newTask->l3miss_value = -1;
    newTask->llcMiss_value = -1; newTask->remoteFwd_value = -1; 
    newTask->remoteHitm_value = -1; newTask->remoteDram_value = -1;
    newTask->lastMigration = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    size_t numDoubles = (1 << 20) / sizeof(double);
    newTask->bufSize = numDoubles;
    newTask->buffer = (double *)numa_alloc_onnode(numDoubles * sizeof(double), newTask->assignedSocket);
    
    if (newTask->buffer) {
        for (size_t k = 0; k < numDoubles; k++) newTask->buffer[k] = (double)rand() / RAND_MAX;
    }

    this->ti = newTask.get(); 
    parent_scheduler->tasks.push_back(std::move(newTask)); 

    t = std::thread( [ith, this, tb, worker_num](){
      this->csched = new Coro_Scheduler(ith, &taskQ);
      this->stop = false;

      Charm::thread_id = ith;
      // Self-affinity at startup
      set_thread_affinity(pthread_self(), ith);
      
      this->ti->tid.store(syscall(SYS_gettid));

      this->eventsCounter = new PerfCounter();
      this->eventsCounter->startCounters();

      tb->wait();
      this->add_task_worker(worker_num);
      csched->await();
    });
    
    this->ti->threadHandle = t.native_handle();
}

void SamWorker::task_worker(){
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

void SamWorker::finish(){
    this->stop = true;
    if(eventsCounter) eventsCounter->stopCounters();
}

void SamWorker::yield(){
    auto current_time_now = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time_now - time);
    
    if (elapsed_time.count() >= SCHEDULER_TIMER_SAM) {
    
        if(this->eventsCounter) {
            ti->l2miss_value = this->eventsCounter->getCounter("L1-DCACHE-LOAD-MISSES");
            ti->l3hit_value = this->eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
            ti->l3miss_value = this->eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL");
            ti->llcMiss_value = this->eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT");
            ti->remoteFwd_value = this->eventsCounter->getCounter("STORE_TO_LOAD_FORWARD");
            ti->remoteHitm_value = this->eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL");
            ti->remoteDram_value = this->eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_RMT");

            this->eventsCounter->resetCounter("L1-DCACHE-LOAD-MISSES");
            this->eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
            this->eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_LCL");
            this->eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:EXT_CACHE_RMT");
            this->eventsCounter->resetCounter("STORE_TO_LOAD_FORWARD");
            this->eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_LCL");
            this->eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:MEM_IO_RMT");

            deriveMetrics(*ti);
        }
      
        parent_scheduler->runSAM();

        time = std::chrono::steady_clock::now();
    }

    csched->coroutine_yield();
}


SamScheduler::SamScheduler(int worker_num){
  num_threads = THREAD_SIZE;
  start_barrier = new Thread_Barrier( num_threads+1 );
  std::srand((unsigned)std::time(nullptr));

  std::string vendor = detectCpuVendor();
  if (vendor == "GenuineIntel") {
      gEvt = { 0x3424, 0x3425, 0x3426, 0x412E, 0x01b7, 0x02b7, 0x01cb };
      std::cout << "[SAM] Detected Intel CPU.\n";
  } else {
      gEvt = { 0, 0, 0, 0, 0, 0, 0 };
      std::cout << "[SAM] Non-Intel CPU: generic counters.\n";
  }

  tasks.reserve(THREAD_SIZE);
  
  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new SamWorker(i, worker_num, start_barrier, workers, this));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  while(start_barrier->get_cnt() != 1);
}

void SamScheduler::runSAM() {
    if (!samMutex.try_lock()) {
        return; 
    }
    
    auto &tv = this->tasks; 

    std::vector<int> socketLoad(NUMA_DOMAINS_SAM, 0);
    for (auto &t : tv) {
        socketLoad[t->assignedSocket]++;
    }

    // Step 1: High InterSocket Coherence
    std::vector<int> interTasks;
    for (size_t i = 0; i < tv.size(); i++) {
        if (tv[i]->metrics.interSocket > C_rT)
            interTasks.push_back((int)i);
    }
    
    if (!interTasks.empty()) {
        int bestSocket = 0, minLoad = socketLoad[0];
        for (int s = 1; s < NUMA_DOMAINS_SAM; s++) {
            if (socketLoad[s] < minLoad) {
                bestSocket = s;
                minLoad = socketLoad[s];
            }
        }
        
        for (int idx : interTasks) {
            if (tv[idx]->assignedSocket != bestSocket && socketLoad[bestSocket] < CORES_PER_NUMA_NODE_SAM) {
                socketLoad[tv[idx]->assignedSocket]--;
                socketLoad[bestSocket]++;
                tv[idx]->assignedSocket = bestSocket;
            }
        }
    }

    // Step 2: Remote DRAM
    for (auto &t : tv) {
        if (t->metrics.remoteDram > R_rT) {
            if (t->assignedSocket != t->originalSocket) {
                if (socketLoad[t->originalSocket] < CORES_PER_NUMA_NODE_SAM) {
                    socketLoad[t->assignedSocket]--;
                    socketLoad[t->originalSocket]++;
                    t->assignedSocket = t->originalSocket;
                }
            }
        }
    }

    // Step 3: LLC Misses
    for (auto &t : tv) {
        if (t->metrics.llcMiss > M_rT) {
            int bestSocket = t->assignedSocket;
            int minL = socketLoad[bestSocket];
            for (int s = 0; s < NUMA_DOMAINS_SAM; s++) {
                if (socketLoad[s] < minL) {
                    bestSocket = s;
                    minL = socketLoad[s];
                }
            }
            if (bestSocket != t->assignedSocket) {
                if (socketLoad[bestSocket] < CORES_PER_NUMA_NODE_SAM) {
                    socketLoad[t->assignedSocket]--;
                    socketLoad[bestSocket]++;
                    t->assignedSocket = bestSocket;
                }
            }
        }
    }

    // Step 4: Apply Migration
    auto now = std::chrono::steady_clock::now();
    std::vector<int> coreIdx(NUMA_DOMAINS_SAM, 0);
    for (auto &t : tv) {
        int targetCoreInSocket = coreIdx[t->assignedSocket] % CORES_PER_NUMA_NODE_SAM;
        coreIdx[t->assignedSocket]++;
        
        if (t->assignedCore != targetCoreInSocket) {
          auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - t->lastMigration);
          if (duration.count() >= 1) { 
            t->lastMigration = now;
            migrateTask(*t, t->assignedSocket, targetCoreInSocket);
          }
        }
    }

    samMutex.unlock();
}

void SamScheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void SamScheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void SamScheduler::start_perf_counters() {
}

void SamScheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void SamScheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

std::vector<WorkerImpl*> SamScheduler::get_workers_impl() {
    std::vector<WorkerImpl*> base_workers;
    for(auto* w : workers) base_workers.push_back(w);
    return base_workers;
}

} // namespace Charm