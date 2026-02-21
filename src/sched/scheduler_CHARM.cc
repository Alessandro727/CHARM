/* scheduler_CHARM.cc */
#include <future>
#include <thread>
#include <atomic>
#include <numaif.h>

#ifdef NUMA_AWARE
#include <numa.h>
#endif

#include "sched/scheduler.h" // Include il Router per avere accesso alle variabili globali esterne se servono
#include "scheduler_CHARM.h" // Include la definizione della classe concreta

namespace Charm{


CharmWorker::CharmWorker(size_t ith, int worker_num, Thread_Barrier* tb, std::vector<CharmWorker*>& all_workers)
    : rank(ith), stop(false), all_workers(all_workers) {
    current_chiplet = ith / CORES_PER_CHIPLET;
  
    t = std::thread( [ith, this, tb, worker_num](){
    this->csched = new Coro_Scheduler(ith, &taskQ);
    this->stop = false;

#ifdef NUMA_AWARE
    int threads = THREAD_SIZE;
#endif

    this->eventsCounter = new PerfCounter();
    this->eventsCounter->startCounters();
    this->time = std::chrono::steady_clock::now();

    cpu_set_t cpuset;
    pthread_t current_thread = pthread_self(); 

    CPU_ZERO(&cpuset);
    CPU_SET(ith, &cpuset);

    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cout << "Error: Unable to set thread affinity!\n";
    }

    set_thread_affinity(rank);
    int numa_node = rank / CORES_PER_NUMA_NODE;

    struct bitmask* nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, numa_node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);

    // Nota: thread_id è gestito dal sistema globale, ma qui lo usiamo localmente al thread fisico
    // Charm::thread_id = ith; // Se thread_id è extern, ok, ma meglio non toccarlo qui se non necessario.
    Charm::thread_id = ith;

    tb->wait();

    /* create normal workers */
    this->add_task_worker(worker_num);
    
    csched->await();
    
  });

}


void CharmWorker::task_worker(){
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
      maybe_yield(); // Chiama il metodo membro, non quello globale
    }
  }
}

void CharmWorker::set_thread_affinity(int core_id) {
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
 
void CharmWorker::yield(){
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(current_time - time);
    
    if (elapsed_time >= std::chrono::milliseconds(SCHEDULER_TIMER)) {
      uint64_t counter = eventsCounter->getCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
      uint64_t result = counter / elapsed_time.count()*SCHEDULER_TIMER;
      
      if (result >= RMT_CHIP_ACCESS_RATE) {
        increase_spread();
      } else {
        if (result > 0) {
          decrease_spread();
        }
      }
      time = std::chrono::steady_clock::now();
      eventsCounter->resetCounter("ANY_DATA_CACHE_FILLS_FROM_SYSTEM:INT_CACHE");
    }

    csched->coroutine_yield();
}

// --- Scheduler Implementation ---

CharmScheduler::CharmScheduler(int worker_num){
  num_threads = THREAD_SIZE;
  start_barrier = new Thread_Barrier( num_threads+1 );

  for(size_t i = 0; i < num_threads; ++i) {
    workers.push_back(new CharmWorker(i, worker_num, start_barrier, workers));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  while(start_barrier->get_cnt() != 1);
}

void CharmScheduler::spawn_coroutine(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine(f, tag);
}

void CharmScheduler::spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag){
  workers[ith]->spawn_coroutine_periodic(f, tag);
}

void CharmScheduler::start_perf_counters() {
  for(auto& w : workers){
    w->eventsCounter = new PerfCounter();
    w->eventsCounter->startCounters();
  }
}

void CharmScheduler::finish(){
  for(auto pw : workers){
    pw->finish();
  }
}

void CharmScheduler::await(){
  for(auto pw : workers){
     pw->await(); 
  }
}

std::vector<WorkerImpl*> CharmScheduler::get_workers_impl() {
    std::vector<WorkerImpl*> base_workers;
    for(auto* w : workers) base_workers.push_back(w);
    return base_workers;
}

}//namespace Charm