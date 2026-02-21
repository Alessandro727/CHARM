/* scheduler_ASYM.cc */
#include <future>
#include <thread>
#include <atomic>
#include <numaif.h>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h" 
#include "scheduler_ASYM.h"

namespace Charm{

AsymWorker::AsymWorker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<AsymWorker*>& all_workers, 
               std::unordered_map<int, std::vector<int>> nodeToCores, std::vector<ThreadInfoEx> allThreads,
               AsymScheduler* scheduler_ptr)
    : rank(ith), stop(false), all_workers(all_workers), nodeToCores(nodeToCores), allThreads(allThreads), parent_scheduler(scheduler_ptr) {
  
  this->time = std::chrono::steady_clock::now();

  t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
    int sockets = numa_num_configured_nodes();
    int threads_per_node = threads / sockets;
    int sockets_id = ith / threads_per_node;
    int retval = numa_run_on_node(sockets_id);
    ASSERT_CHARM(retval == 0, "Failed to set thread affinity to target numa node");
#endif

    Charm::thread_id = ith;
    tb->wait();

    this->add_task_worker(worker_num);
    csched->await();
  });

  // Local init for ThreadInfoEx
  ThreadInfoEx th;
  th.threadId = ith;
  th.appId = 100;
  th.assignedNode = ith % NUMA_DOMAINS_ASYM;
  th.assignedCore = nodeToCores[ th.assignedNode ][0];

  size_t numDoubles = (1 << 20) / sizeof(double);
  th.computeData.size = numDoubles;
  th.computeData.buffer = new double[numDoubles];
  for (size_t i = 0; i < numDoubles; i++) {
      th.computeData.buffer[i] = static_cast<double>(rand()) / RAND_MAX;
  }
  this->allThreads.push_back(th);
  
#ifndef NUMA_AWARE
  cpu_set_t cpuset;
  CPU_ZERO( &cpuset );
  CPU_SET( ith, &cpuset );
  pthread_setaffinity_np( t.native_handle(),  sizeof(cpuset), &cpuset );
#endif
}

void AsymWorker::task_worker(){
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

void AsymWorker::set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    
    int numa_node = core_id / CORES_PER_NUMA_NODE_ASYM;
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);
}

void AsymWorker::yield(){
  ASSERT_CHARM(allow_yield, "should not yield");
  
  auto current_time_now = std::chrono::steady_clock::now();
  auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time_now - this->time);

  if (elapsed_time.count() >= SCHEDULER_TIMER_ASYM) {
      
      auto sys = this->parent_scheduler; 

      std::vector<Cluster> clusters = formClusters(sys);

      for (auto &cl : clusters) {
          cl.Crbw = 0.0;
          cl.Cweight = 1.0;
          for (auto *th : cl.threads) {
              long long cnt = measureCacheMisses(th->tid, th->assignedCore, 100);
              cl.Crbw += (double)cnt;
          }
      }

      std::vector<Placement> placements = computePlacements(clusters, sys->nodes);
      
      double Maxwbw = 0.0;
      for (auto &pl : placements) {
          pl.Pwbw = computeClusterBandwidth(pl, clusters);
          Maxwbw = std::max(Maxwbw, pl.Pwbw);
      }
      
      for (auto &pl : placements) {
          if (pl.Pwbw >= 0.90 * Maxwbw) {
              pl.Pmm = computeMemoryMigrationCost(pl, sys->allApps);
          }
      }
      
      double bestPmm = 1e15;
      Placement bestPlacement;
      bool found = false;
      for (auto &pl : placements) {
          if (pl.Pwbw >= 0.90 * Maxwbw && pl.Pmm < bestPmm) {
              bestPmm = pl.Pmm;
              bestPlacement = pl;
              found = true;
          }
      }
      
      if (found) {
          std::vector<Application*> migratedApps;
          identifyMigratedApplications(bestPlacement, clusters, migratedApps, sys);
          bool blockMigration = false;
          for (auto *A : migratedApps) {
              double sumNodes = 0.0;
              for (auto &cl : clusters) {
                  if (clusterBelongsToApp(cl, A->appId)) {
                      int newNode = bestPlacement.clusterToNode.at(&cl);
                      sumNodes += (A->Amm[newNode] * 0.3);
                  }
              }
              double overheadVal = A->Atm + sumNodes;
              if (overheadVal > 0.05 * A->Att) {
                  blockMigration = true;
              }
          }
          
          if (!blockMigration) {
              performThreadMigration(bestPlacement, clusters, sys);
              dynamicMemoryMigration(bestPlacement, clusters, sys);
          }
          
          for (auto *A : migratedApps) {
              if (A->oldA > 0.90) {
                  fullyMigrateMemory(*A);
              }
          }
      }

      // Reset del timer dopo aver eseguito l'algoritmo
      this->time = std::chrono::steady_clock::now();
  }

  // 2. Yield leggero (sempre eseguito)
  csched->coroutine_yield();
}

AsymScheduler::AsymScheduler(int worker_num){
    num_threads = THREAD_SIZE;
    start_barrier = new Thread_Barrier( num_threads+1 );
    
    for (int n = 0; n < NUMA_DOMAINS_ASYM; n++) {
        NodeInfo node; node.nodeId = n;
        nodes.push_back(node);
        std::vector<int> cores;
        for (int c = n * CORES_PER_NUMA_NODE_ASYM; c < (n+1)*CORES_PER_NUMA_NODE_ASYM; c++) {
            cores.push_back(c);
        }
        nodeToCores[n] = cores;
    }

    const int totalThreads = num_threads;
    allThreads.reserve(totalThreads);
    for (int i=0; i<totalThreads; ++i) {
       ThreadInfoEx th;
       th.threadId = i; 
       th.tid = i + 1000; 
       th.appId = 100;
       allThreads.push_back(th);
    }

    Application app;
    app.appId = 100;
    app.Att = 100.0;
    app.Atm = 1.0;
    app.oldA = 0.95;
    app.Amm[0] = 0.2; app.Amm[1] = 0.3; 
    app.memSize = 32 << 20;
    app.memPtr = allocateOnNode(app.memSize, 0);
    allApps.push_back(app);

  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new AsymWorker(i, worker_num, start_barrier, workers, nodeToCores, allThreads, this));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  while(start_barrier->get_cnt() != 1);
}

void AsymScheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void AsymScheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void AsymScheduler::start_perf_counters() {
  for(auto& w : workers){
    w->eventsCounter = new PerfCounter();
    w->eventsCounter->startCounters();
  }
}

void AsymScheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void AsymScheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

std::vector<WorkerImpl*> AsymScheduler::get_workers_impl() {
    std::vector<WorkerImpl*> base_workers;
    for(auto* w : workers) base_workers.push_back(w);
    return base_workers;
}

// Dummy methods
void AsymWorker::update_location() {}
void AsymWorker::increase_spread() {}
void AsymWorker::decrease_spread() {}
int AsymWorker::calculateCore(int core) { return 0; }
void AsymWorker::spread() {}
void AsymWorker::compact() {}
void AsymWorker::assignBalanced() {}

}//namespace Charm