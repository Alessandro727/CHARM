/* sched/scheduler_ring.cc */
#include "sched/scheduler_ring.h"

namespace Charm {

// --------------------------------------------------------
// RING WORKER IMPLEMENTATION
// --------------------------------------------------------

RingWorker::RingWorker(size_t ith, int worker_num, Thread_Barrier* tb)
    : Worker(ith, worker_num, tb) {
    
    // Specific logic for Ring: Bind 1-to-1 immediately
    set_thread_affinity(ith);
}

void RingWorker::yield() {
    // Ring Strategy: simple yield, no counters, no migration
    csched->coroutine_yield();
}

// --------------------------------------------------------
// RING SCHEDULER IMPLEMENTATION
// --------------------------------------------------------

RingScheduler::RingScheduler(int worker_num) : Scheduler(THREAD_SIZE) {
    // Create the specific RingWorkers
    for(size_t i = 0; i < num_threads; ++i) {
        workers.push_back(new RingWorker(i, worker_num, start_barrier));
    }
    
    // Wait for the workers to hit the barrier in their threads
    while(start_barrier->get_cnt() != 1);
}

} // namespace Charm