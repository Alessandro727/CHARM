/* sched/scheduler_sam.cc */
#include "sched/scheduler_sam.h"

namespace Charm {

SamScheduler::SamScheduler(int worker_num) : Scheduler(THREAD_SIZE) {
    // *** FIX: Initialize the global pointer IMMEDIATELY ***
    global_scheduler = this;

    tasks.reserve(THREAD_SIZE);
    for(size_t i = 0; i < num_threads; ++i) {
        workers.push_back(new SamWorker(i, worker_num, start_barrier, tasks));
    }
    while(start_barrier->get_cnt() != 1);
}

SamWorker::SamWorker(size_t ith, int worker_num, Thread_Barrier* tb, 
                     std::vector<std::unique_ptr<TaskInfo>>& tasks)
    : Worker(ith, worker_num, tb) {
    
    auto ti = std::unique_ptr<TaskInfo>(new TaskInfo());
    ti->id = ith;
    ti->tid.store(syscall(SYS_gettid));
    ti->assignedSocket = ith % NUMA_DOMAINS;
    ti->assignedCore = ith;
    ti->originalSocket = ti->assignedSocket;
    ti->lastMigration = std::chrono::steady_clock::now();
    
    myTaskInfo = ti.get();
    
    // Allocate dummy buffer for memory migration testing
    size_t numDoubles = (1 << 20) / sizeof(double);
    ti->bufSize = numDoubles;
    ti->buffer = (double *)numa_alloc_onnode(numDoubles * sizeof(double), ti->assignedSocket);

    tasks.push_back(std::move(ti));
    
    set_thread_affinity(ith);
    time = std::chrono::steady_clock::now();
}

void SamWorker::yield() {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - time).count() >= SCHEDULER_TIMER) {
        if (eventsCounter) {
            // Simplified counter reading
            myTaskInfo->l2miss_val = eventsCounter->getCounter("L1-DCACHE-LOAD-MISSES");
            myTaskInfo->metrics.interSocket = myTaskInfo->remoteFwd_val + myTaskInfo->remoteHitm_val;
        }
        time = now;
    }

    csched->coroutine_yield();
    static_cast<SamScheduler*>(global_scheduler)->runSAM();
}

void SamScheduler::runSAM() {
    std::vector<int> socketLoad(NUMA_DOMAINS, 0);
    for (auto &t : tasks) socketLoad[t->assignedSocket]++;

    for (auto &t : tasks) {
        if (t->metrics.interSocket > C_rT) {
             int bestSocket = 0, minLoad = socketLoad[0];
            for (int s = 1; s < NUMA_DOMAINS; s++) {
                if (socketLoad[s] < minLoad) {
                    bestSocket = s;
                    minLoad = socketLoad[s];
                }
            }
             if (t->assignedSocket != bestSocket) {
                socketLoad[t->assignedSocket]--;
                socketLoad[bestSocket]++;
                t->assignedSocket = bestSocket;
            }
        }
    }
}

} // namespace Charm