/* sched/scheduler_sam.h */
#ifndef _SCHEDULER_SAM_H_
#define _SCHEDULER_SAM_H_

#include "scheduler.h"
#include <memory>
#include <atomic>

#define C_rT 5.5e5
#define R_rT 2.7e6
#define M_rT 7.5e7

namespace Charm {

struct CoherenceMetrics {
    double l2miss, l3hit, l3miss, llcMiss;
    double remoteFwd, remoteHitm, remoteDram;
    double intraSocket, interSocket;
};

struct TaskInfo {
    int id;
    std::atomic<pid_t> tid;
    int assignedSocket;
    int assignedCore;
    int originalSocket;
    
    // Values
    double l2miss_val, l3hit_val, l3miss_val;
    double llcMiss_val, remoteFwd_val, remoteHitm_val, remoteDram_val;
    
    CoherenceMetrics metrics;
    double* buffer;
    size_t bufSize;
    std::chrono::steady_clock::time_point lastMigration;
};

class SamWorker : public Worker {
public:
    SamWorker(size_t ith, int worker_num, Thread_Barrier* tb, 
              std::vector<std::unique_ptr<TaskInfo>>& tasks);

    void yield() override;

private:
    std::chrono::steady_clock::time_point time;
    TaskInfo* myTaskInfo = nullptr;
};

class SamScheduler : public Scheduler {
public:
    SamScheduler(int worker_num);
    void runSAM();
    std::vector<std::unique_ptr<TaskInfo>> tasks;
};

} // namespace Charm
#endif