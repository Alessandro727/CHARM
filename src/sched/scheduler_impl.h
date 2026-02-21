/* scheduler_impl.h */
#ifndef _SCHEDULER_IMPL_H_
#define _SCHEDULER_IMPL_H_

#include <vector>
#include <functional>
#include "tasking/task_queue.h"
#include "sched/thread_barrier.h"

namespace Charm {

class Condition; 

// INTERFACE

class WorkerImpl {
public:
    virtual ~WorkerImpl() {}
    virtual void yield() = 0;
    virtual void maybe_yield() = 0;
    virtual void idle() = 0;
    virtual void wait(Condition* c) = 0;
    virtual void signal(Condition* c) = 0;
    virtual void spawn_coroutine(std::function<void()> f, int tag = 0) = 0;
    virtual void spawn_coroutine_periodic(std::function<void()> f, int tag = 0) = 0;
    virtual void await() = 0;
    virtual void finish() = 0;
    virtual void private_enqueue_impl(std::function<void()> f) = 0; 
    
    virtual TaskQueue& get_task_queue() = 0; 
};

class SchedulerImpl {
public:
    virtual ~SchedulerImpl() {}
    virtual void start() = 0;
    virtual void finish() = 0;
    virtual void await() = 0;
    virtual void spawn_coroutine(size_t ith, std::function<void()> f, int tag = 0) = 0;
    virtual void spawn_coroutine_periodic(size_t ith, std::function<void()> f, int tag = 0) = 0;
    
    virtual std::vector<WorkerImpl*> get_workers_impl() = 0;
};

}
#endif