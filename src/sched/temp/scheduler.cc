/* sched/scheduler.cc */
#include "sched/scheduler.h"

// Include Specific Implementations
// *** CRITICAL: These must all be included so the Factory can see the classes ***
#include "sched/scheduler_adaptive.h"
#include "sched/scheduler_ring.h"
#include "sched/scheduler_sam.h" 
#include "sched/scheduler_asym.h"

namespace Charm {

Scheduler* global_scheduler = nullptr;
bool global_exit_flag = false;
thread_local int thread_id;
thread_local bool allow_yield = true;

// --------------------------------------------------------
// GLOBAL STATS (Definition)
// --------------------------------------------------------
// These definitions are required here because they are "extern" in scheduler.h
// and likely declared "extern" in memlog.h, but need exactly one storage location.
// NOTE: If memlog.cc already defines these, delete this block to avoid double-definition.
// Based on typical CHARM structure, memlog.h declares thread_local, but sometimes 
// global fallback is used. If linker fails saying "multiple definition", remove these 6 lines.
/* uint64_t msg_on_heap = 0;
uint64_t msg_1_cacheline = 0;
uint64_t msg_2_cacheline = 0;
uint64_t msg_3_cacheline = 0;
uint64_t msg_4_cacheline = 0;
uint64_t msg_x_cacheline = 0; 
*/

// --- Worker Implementation ---
Worker::Worker(size_t ith, int worker_num, Thread_Barrier* tb) 
    : rank(ith), barrier(tb), worker_num_init(worker_num) {
    t = std::thread(&Worker::worker_entry, this);
}

Worker::~Worker() {
    if(t.joinable()) t.join();
    if(csched) delete csched;
    if(eventsCounter) delete eventsCounter;
}

void Worker::worker_entry() {
    this->csched = new Coro_Scheduler(rank, &taskQ);
    this->stop = false;
    thread_id = rank;
    
    barrier->wait(); // Sync start
    
    this->add_task_worker(worker_num_init);
    csched->await();
}

void Worker::task_worker(){
    Task victim;
    bool finded = false;
    for(;;){
        finded = taskQ.try_private(&victim);
        if(!finded){
            if(!this->stop) idle();
            else break;
        } else {
            csched->active_coro_num++;
            victim();
            csched->active_coro_num--;
            maybe_yield();
        }
    }
}

// Implementations of helpers
void Worker::maybe_yield() { csched->coroutine_maybe_yield(); }
void Worker::idle() { csched->coroutine_idle(); }
void Worker::wait(Condition* c) { csched->coroutine_wait(c); }
void Worker::signal(Condition* c) { csched->coroutine_signal(c); }
void Worker::spawn_coroutine(std::function<void()> f, int tag) { csched->coroutine_create(f, tag); }
void Worker::spawn_coroutine_periodic(std::function<void()> f, int tag) { csched->coroutine_create_periodic(f, tag); }
void Worker::await() { if(t.joinable()) t.join(); }
void Worker::finish() { this->stop = true; }

void Worker::add_task_worker(int num){
    while(num-- > 0){
      spawn_coroutine([this]{
        this->task_worker();
      });
    }
}

void Worker::set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);

#ifdef NUMA_AWARE
    int numa_node = core_id / CORES_PER_NUMA_NODE;
    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);
#endif
}

// --- Scheduler Implementation ---
Scheduler::Scheduler(int num) : num_threads(num) {
    start_barrier = new Thread_Barrier(num + 1);
}

Scheduler::~Scheduler() {
    for(auto w : workers) delete w;
    delete start_barrier;
}

// *** THE FACTORY ***
Scheduler* Scheduler::create(std::string type, int worker_num) {
    if (type == "RING") {
        std::cout << "[SCHEDULER] Mode: RING" << std::endl;
        return new RingScheduler(worker_num);
    } 
    else if (type == "SAM") {
        std::cout << "[SCHEDULER] Mode: SAM" << std::endl;
        return new SamScheduler(worker_num); 
    } 
    else if (type == "ASYM") {
        std::cout << "[SCHEDULER] Mode: ASYM" << std::endl;
        return new AsymScheduler(worker_num);
    }
    else {
        std::cout << "[SCHEDULER] Mode: CHARM" << std::endl;
        return new AdaptiveScheduler(worker_num);
    }
}

void Scheduler::start_perf_counters() {
    for(auto& w : workers){
        w->eventsCounter = new PerfCounter();
        w->eventsCounter->startCounters();
    }
}

void Scheduler::finish() { for(auto w : workers) w->finish(); }
void Scheduler::await() { for(auto w : workers) w->await(); }
Worker* Scheduler::get_cur_worker() { return workers[thread_id]; }
Worker* Scheduler::get_worker(size_t index) { return workers[index]; }
int Scheduler::get_id() { return thread_id; }

void Scheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag) {
    workers[ith]->spawn_coroutine(f, tag);
}
void Scheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag) {
    workers[ith]->spawn_coroutine_periodic(f, tag);
}

void yield() { global_scheduler->get_cur_worker()->yield(); }
void maybe_yield() { global_scheduler->get_cur_worker()->maybe_yield(); }

} // namespace Charm