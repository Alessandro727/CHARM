/* sched/scheduler_asym.h */
#ifndef _SCHEDULER_ASYM_H_
#define _SCHEDULER_ASYM_H_

#include "scheduler.h"
#include <map>
#include <vector>
#include <thread>
#include <unordered_map>

namespace Charm {

struct Application {
    int appId;
    double Atm;   
    double Att;   
    double oldA; 
    std::unordered_map<int, double> Amm; 
    void* memPtr;
    size_t memSize;
};

struct ThreadInfoEx {
    int threadId;
    int appId;
    int assignedNode;
    int assignedCore;
    std::thread::native_handle_type pthreadHandle;
};

struct NodeInfo { int nodeId; };

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

class AsymWorker : public Worker {
public:
    AsymWorker(size_t ith, int worker_num, Thread_Barrier* tb);
    void yield() override;
};

class AsymScheduler : public Scheduler {
public:
    AsymScheduler(int worker_num);
    void runAsymRebalancing();

    std::vector<Application> allApps;
    std::vector<ThreadInfoEx> allThreads;
    std::vector<NodeInfo> nodes;
    std::unordered_map<int, std::vector<int>> nodeToCores;
};

} // namespace Charm
#endif