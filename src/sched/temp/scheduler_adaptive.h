/* sched/scheduler_adaptive.h */
#ifndef _SCHEDULER_ADAPTIVE_H_
#define _SCHEDULER_ADAPTIVE_H_

#include "sched/scheduler.h"
#include <chrono>

#define RMT_CHIP_ACCESS_RATE 300

namespace Charm {

class AdaptiveWorker : public Worker {
public:
    AdaptiveWorker(size_t ith, int worker_num, Thread_Barrier* tb)
        : Worker(ith, worker_num, tb) {
        
        // Initial Placement
        update_location(); 
        time = std::chrono::steady_clock::now();
    }

    void update_location() {
        if (spread_rate <= 0 || spread_rate > CHIPLETS || THREAD_SIZE > spread_rate * CORES_PER_CHIPLET) return;

        int chunk_size = CORES_PER_CHIPLET / spread_rate;
        if (chunk_size < 1) chunk_size = 1;

        int machine_capacity = CHIPLETS * chunk_size;
        int pass = rank / machine_capacity; 

        int current_chiplet = (rank / chunk_size) % CHIPLETS;
        int current_slot = (rank % chunk_size) + (pass * chunk_size);

        int core = current_chiplet * CORES_PER_CHIPLET + current_slot;
        set_thread_affinity(core);
    }

    void increase_spread() {
        if (spread_rate < CHIPLETS) {
            spread_rate++;
            update_location();
        }
    }

    void decrease_spread() {
        if (spread_rate > 1) {
            spread_rate--;
            update_location();
        }
    }

    void yield() override {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
        
        if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {
            if (eventsCounter) {
                uint64_t counter = eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
                double seconds = elapsed_time.count() / 1000000.0;
                
                if(seconds > 0) {
                    uint64_t rate = counter / seconds; 
                    if (rate >= RMT_CHIP_ACCESS_RATE) {
                        increase_spread();
                    } else if (rate > 0) {
                        decrease_spread();
                    }
                }
                eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
            }
            time = std::chrono::steady_clock::now();
        }
        csched->coroutine_yield();
    }

private:
    std::chrono::steady_clock::time_point time;
    int spread_rate = 1;
};

class AdaptiveScheduler : public Scheduler {
public:
    AdaptiveScheduler(int worker_num) : Scheduler(THREAD_SIZE) {
        for(size_t i = 0; i < num_threads; ++i) {
            workers.push_back(new AdaptiveWorker(i, worker_num, start_barrier));
        }
        while(start_barrier->get_cnt() != 1);
    }
};

} // namespace Charm
#endif