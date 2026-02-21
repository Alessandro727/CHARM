/* sched/scheduler_ring.h */
#ifndef _SCHEDULER_RING_H_
#define _SCHEDULER_RING_H_

#include "scheduler.h"

namespace Charm {

class RingWorker : public Worker {
public:
    // Declaration only
    RingWorker(size_t ith, int worker_num, Thread_Barrier* tb);
    
    // Override yield
    void yield() override;
};

class RingScheduler : public Scheduler {
public:
    // Declaration only
    RingScheduler(int worker_num);
};

} // namespace Charm
#endif