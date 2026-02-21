/* scheduler.cc */
#include "scheduler.h"
#include "scheduler_CHARM.h"
#include "scheduler_RING.h"
#include "scheduler_ASYM.h"
#include "scheduler_SAM.h" // NUOVO INCLUDE
#include <cstdlib>
#include <iostream>

namespace Charm {

// Nota: Variabili contatore definite altrove

Scheduler * global_scheduler;
bool global_exit_flag;
thread_local int thread_id;
thread_local bool allow_yield=true;

Scheduler::Scheduler(int worker_num) {
    const char* env_sched = std::getenv("SCHEDULER_TYPE");
    std::string sched_type = (env_sched) ? std::string(env_sched) : "CHARM"; 

    if (sched_type == "RING") {
        std::cout << "[CHARM SYSTEM] Initializing RING Scheduler..." << std::endl;
        pImpl = new RingScheduler(worker_num);
    } 
    else if (sched_type == "ASYM") {
        std::cout << "[CHARM SYSTEM] Initializing ASYM Scheduler..." << std::endl;
        pImpl = new AsymScheduler(worker_num);
    }
    else if (sched_type == "SAM") { // NUOVO BLOCCO
        std::cout << "[CHARM SYSTEM] Initializing SAM Scheduler..." << std::endl;
        pImpl = new SamScheduler(worker_num);
    }
    else {
        std::cout << "[CHARM SYSTEM] Initializing CHARM Scheduler..." << std::endl;
        pImpl = new CharmScheduler(worker_num);
    }

    std::vector<WorkerImpl*> implWorkers = pImpl->get_workers_impl();
    num_threads = implWorkers.size();
    
    for(size_t i=0; i < implWorkers.size(); ++i) {
        workers.push_back(new Worker(implWorkers[i]));
    }
}

Scheduler::~Scheduler() {
    for(auto w : workers) delete w;
    delete pImpl;
}

void Scheduler::await() { pImpl->await(); }
void Scheduler::finish() { pImpl->finish(); }
void Scheduler::start() { pImpl->start(); }

size_t Scheduler::get_size() { return num_threads; }

int Scheduler::get_id() { return thread_id; }

Worker* Scheduler::get_cur_worker() {
    return workers[thread_id];
}

Worker* Scheduler::get_worker(size_t index) {
    return workers[index];
}

void Scheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag) {
    pImpl->spawn_coroutine(ith, f, tag);
}

void Scheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag) {
    pImpl->spawn_coroutine_periodic(ith, f, tag);
}

void Scheduler::start_perf_counters() {
    // Tenta il cast per ogni tipo concreto per avviare i contatori
    if (CharmScheduler* s = dynamic_cast<CharmScheduler*>(pImpl)) s->start_perf_counters();
    if (AsymScheduler* s = dynamic_cast<AsymScheduler*>(pImpl)) s->start_perf_counters();
    if (SamScheduler* s = dynamic_cast<SamScheduler*>(pImpl)) s->start_perf_counters();
}

std::string Scheduler::get_active_scheduler_type() {
    if (dynamic_cast<RingScheduler*>(pImpl)) return "RING";
    if (dynamic_cast<CharmScheduler*>(pImpl)) return "CHARM";
    if (dynamic_cast<AsymScheduler*>(pImpl)) return "ASYM";
    if (dynamic_cast<SamScheduler*>(pImpl)) return "SAM";
    return "UNKNOWN";
}

void yield(){
  ASSERT_CHARM(allow_yield, "should not yield");
  global_scheduler->get_cur_worker()->yield();
}

void maybe_yield(){
  ASSERT_CHARM(allow_yield, "should not yield");
  global_scheduler->get_cur_worker()->maybe_yield();
}

}