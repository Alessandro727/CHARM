/* scheduler.cc
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

 
#include <future>
#include <thread>
#include <atomic>
#include <numaif.h>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h"
#include "sched/coroutine.h"
#include "tasking/task_queue.h"

namespace Charm{

Scheduler * global_scheduler;
bool global_exit_flag;

thread_local int thread_id;
thread_local bool allow_yield=true;

Worker::Worker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<Worker*>& all_workers, 
               std::unordered_map<int, std::vector<int>> nodeToCores, std::vector<ThreadInfoEx> allThreads)
    : rank(ith), stop(false), all_workers(all_workers), nodeToCores(nodeToCores), allThreads(allThreads) {
  
  t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    //this->have_tasks = new Condition();
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
    int sockets = numa_num_configured_nodes();
    int threads_per_node = threads / sockets;
    int sockets_id = ith / threads_per_node;
    int retval = numa_run_on_node(sockets_id);
    ASSERT_CHARM(retval == 0, "Failed to set thread affinity to target numa node");
#endif

    thread_id = ith;
    tb->wait();

    /* create normal workers */
    this->add_task_worker(worker_num);

    csched->await();
  });


  ThreadInfoEx th;
  th.threadId = ith;
  th.appId = 100;
  th.assignedNode = ith % NUMA_DOMAINS;
  th.assignedCore = nodeToCores[ th.assignedNode ][0];

  // Allocate and initialize the compute buffer...
  size_t numDoubles = (1 << 20) / sizeof(double);

  th.computeData.size = numDoubles;
  th.computeData.buffer = new double[numDoubles];
  for (size_t i = 0; i < numDoubles; i++) {
      th.computeData.buffer[i] = static_cast<double>(rand()) / RAND_MAX;
  }

  allThreads.push_back(th);
  ThreadInfoEx &storedThread = allThreads.back();
  storedThread.pthreadHandle = t.native_handle();

#ifndef NUMA_AWARE
  cpu_set_t cpuset;
  CPU_ZERO( &cpuset );
  CPU_SET( ith, &cpuset );
  pthread_setaffinity_np( t.native_handle(),  sizeof(cpuset), &cpuset );
#endif
}

void Worker::task_worker(){
  Task victim;
  bool finded = false;
  for(;;){
    finded = taskQ.try_private(&victim);
    /* we may get a task here */
    if(!finded){
      if(!this->stop){
        //wait(this->have_tasks);
        idle();
      }else break;
    }else{
      csched->active_coro_num ++;
      victim();
      csched->active_coro_num --;
      /* to see if we should switch to a periodic worker */
      maybe_yield();
    }
  }
}



void Worker::set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Error setting thread affinity: " << rc << std::endl;
    }
        int numa_node = core_id / CORES_PER_NUMA_NODE;
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);
}

   

Scheduler::Scheduler(int worker_num){
    num_threads = THREAD_SIZE;
    start_barrier = new Thread_Barrier( num_threads+1 );
    
    

    // // Setup 4 nodes, ids 0..3.
    for (int n = 0; n < NUMA_DOMAINS; n++) {
        NodeInfo node; node.nodeId = n;
        nodes.push_back(node);
        // For each node, assign cores: e.g., node0 has cores 0..15, node1: 16..31, etc.
        std::vector<int> cores;
        for (int c = n * CORES_PER_NUMA_NODE; c < (n+1)*CORES_PER_NUMA_NODE; c++) {
            cores.push_back(c);
        }
        nodeToCores[n] = cores;
    }


    const int totalThreads = num_threads;
    allThreads.reserve(totalThreads);
    


    // In a real system, you need a mechanism for each thread to report its TID.
    // For this example, we will simulate that each thread's TID equals its logical ID + 1000.
    for (auto &th : allThreads) {
        th.tid = th.threadId + 1000;
    }

    // Create one application.

  
    Application app;
    app.appId = 100;
    app.Att = 100.0;
    app.Atm = 1.0;
    app.oldA = 0.95;  // 95% memory on old node
    // Overhead per node (simulate 16 cores each)

    app.Amm.insert(std::make_pair(0, 0.2));
    app.Amm.insert(std::make_pair(1, 0.3));
    app.Amm.insert(std::make_pair(2, 0.4));
    app.Amm.insert(std::make_pair(3, 0.5));

    // Allocate a 32MB memory region pinned to node 0
    app.memSize = 32 << 20;

    app.memPtr = allocateOnNode(app.memSize, 0);
    allApps.push_back(app);



  /* no need to use emplace_back */
  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new Worker(i, worker_num, start_barrier, workers, nodeToCores, allThreads));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  /* make sure that all the thread local memory are finished */
  while(start_barrier->get_cnt() != 1);

}

void Scheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void Scheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void Scheduler::start_perf_counters() {
  for(auto& w : workers){
    w->eventsCounter = new PerfCounter();
    w->eventsCounter->startCounters();
  }
}

size_t Scheduler::get_size(){
  return this->num_threads;
}

int Scheduler::get_id(){
  return thread_id;
}

void Scheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void Scheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

void yield(){
  ASSERT_CHARM(allow_yield, "should not yiled");
  auto sys = global_scheduler;

  // Step 1: Form clusters. Here, we group threads by appId.
  std::vector<Cluster> clusters = formClusters(sys);

  // Step 2: For each thread in each cluster, measure memory activity.
  // We update each cluster’s Crbw as the sum of measured cache-misses (per 100ms sampling).
  for (auto &cl : clusters) {
      cl.Crbw = 0.0;
      cl.Cweight = 1.0;
      for (auto *th : cl.threads) {
          // Measure on the thread's assigned core.
          long long cnt = measureCacheMisses(th->tid, th->assignedCore, 100);
          cl.Crbw += (double)cnt;
      }
  }

  
  // Step 4: Compute candidate placements (all possible mappings of clusters to nodes)
  std::vector<Placement> placements = computePlacements(clusters, sys->nodes);
  
  // Step 5: Compute Pwbw for each placement, and record the maximum.
  double Maxwbw = 0.0;
  for (auto &pl : placements) {
      pl.Pwbw = computeClusterBandwidth(pl, clusters);
      Maxwbw = std::max(Maxwbw, pl.Pwbw);
  }
  
  // Step 10–13: For placements with Pwbw >= 90% of Maxwbw, compute memory migration cost.
  for (auto &pl : placements) {
      if (pl.Pwbw >= 0.90 * Maxwbw) {
          pl.Pmm = computeMemoryMigrationCost(pl, sys->allApps);
      }
  }
  
  // Step 14: Choose the placement with the lowest Pmm among those not skipped.
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
  if (!found) {
      std::cout << "[AsymSched] No placement found meeting criteria.\n";
      return;
  }
  
  // Step 15–19: For each migrated application, check overhead constraints.
  std::vector<Application*> migratedApps;
  identifyMigratedApplications(bestPlacement, clusters, migratedApps);
  bool blockMigration = false;
  for (auto *A : migratedApps) {
      double sumNodes = 0.0;
      // For each cluster belonging to this app, sum overhead = Amm[newNode] * 0.3.
      for (auto &cl : clusters) {
          if (clusterBelongsToApp(cl, A->appId)) {
              int newNode = bestPlacement.clusterToNode.at(&cl);
              sumNodes += (A->Amm[newNode] * 0.3);
          }
      }
      double overheadVal = A->Atm + sumNodes;
      if (overheadVal > 0.05 * A->Att) {
          std::cout << "[AsymSched] Overhead too high for App " << A->appId
                    << "; skipping migration.\n";
          blockMigration = true;
      }
  }
  
  
  // Step 20: Migrate threads if not blocked.
  if (!blockMigration) {
      performThreadMigration(bestPlacement, clusters);
  }
  
  // Step 21: Migrate memory for affected applications.
  if (!blockMigration) {
      dynamicMemoryMigration(bestPlacement, clusters);
  }
  
  // Step 22: Wait 2 seconds.
  // std::this_thread::sleep_for(std::chrono::seconds(2));

  // Step 23–27: For each migrated app, if oldA > 90%, fully migrate its memory.
  for (auto *A : migratedApps) {
      if (A->oldA > 0.90) {
          fullyMigrateMemory(*A);
      }
  }

  // std::cout << "[AsymSched] AsymSched algorithm completed.\n";
  global_scheduler->get_cur_worker()->yield();

}

void maybe_yield(){
  ASSERT_CHARM(allow_yield, "should not yiled");
  global_scheduler->get_cur_worker()->maybe_yield();
}


}//namespace Charm
