
set( SOURCES
    utils/IO.h
    utils/memlog.h
    utils/memlog.cc
    utils/state_timer.h
    utils/state_timer.cc
    utils/timestamp.cc
    utils/timestamp.h
    utils/utils.h
    utils/utils.cc
    comm/communicator.h
    comm/communicator.cc
    comm/completion.cc
    comm/completion.h
    comm/message.h
    comm/messagepool.h
    comm/messagepool.cc
    comm/pool_allocator.cc
    comm/pool_allocator.h
    comm/mpienv.cc
    comm/mpienv.h
    comm/verbs.h
    comm/verbs.cc
    sched/barrier.h
    sched/barrier.cc
    sched/context.h
    sched/context.S
    sched/coroutine.h
    sched/coroutine.cc
    sched/scheduler.h
    sched/scheduler.cc
    sched/scheduler_RING.h
    sched/scheduler_RING.cc
    sched/scheduler_SAM.h
    sched/scheduler_SAM.cc
    sched/scheduler_ASYM.h
    sched/scheduler_ASYM.cc
    sched/scheduler_CHARM.h
    sched/scheduler_CHARM.cc
    sched/scheduler_impl.h
    sched/thread_barrier.h
    tasking/task.h
    tasking/task_queue.h
    tasking/task_queue.cc
    pgas/addressing.h
    pgas/allocator.h
    pgas/allocator.cc
    pgas/garray.h
    pgas/vmemory.h
    pgas/vmemory.cc
    pgas/parallelfor.h
    pgas/future.h
    pgas/collective.h
    pgas/rpc.h
    graph/edgelist.h
    graph/edgelist.cc
    graph/graph.h
    graph/graph.cc
    perf/perf_counter.h
    charm.h
    charm.cc
)

enable_language(ASM)
set_source_files_properties(sched/context.S PROPERTIES LANGUAGE ASM COMPILE_FLAGS " -g")

# Flag comuni per rinominare le funzioni globali e le variabili
#set(SCHED_RENAME_FLAGS
#    "global_scheduler=global_scheduler_XXX"
#    "thread_rank=thread_rank_XXX"
#    "thread_size=thread_size_XXX"
#    "my_id=my_id_XXX"
#    "yield=yield_XXX"
#    "maybe_yield=maybe_yield_XXX"
#    "set_allow_yield=set_allow_yield_XXX"
#    "allow_yield=allow_yield_XXX"
#    "delivery_private=delivery_private_XXX"
#    "global_exit_flag=global_exit_flag_XXX"
#    "thread_id=thread_id_XXX"
#)

# Configura scheduler_CHARM.cc
# Sostituiamo XXX con CHARM nelle macro

#string(REPLACE "XXX" "CHARM" CHARM_FLAGS "${SCHED_RENAME_FLAGS}")
#set_source_files_properties(sched/scheduler_CHARM.cc PROPERTIES COMPILE_DEFINITIONS
#    "SCHEDULER_BUILD_IMPL;SCHEDULER_IMPL_CHARM;Scheduler=SchedulerCHARM;Worker=WorkerCHARM;${CHARM_FLAGS}"
#)

# Configura scheduler_RING.cc
# Sostituiamo XXX con RING nelle macro

#string(REPLACE "XXX" "RING" RING_FLAGS "${SCHED_RENAME_FLAGS}")
#set_source_files_properties(sched/scheduler_RING.cc PROPERTIES COMPILE_DEFINITIONS
#    "SCHEDULER_BUILD_IMPL;SCHEDULER_IMPL_RING;Scheduler=SchedulerRING;Worker=WorkerRING;${RING_FLAGS}"
#)



# Add library target
#add_library(Charm ${SOURCES})
#if(BUILD_SHARED_LIBS)
    add_library(Charm SHARED ${SOURCES})
#else()
#    add_library(Charm STATIC ${SOURCES})
#endif()


add_dependencies(Charm tools)
target_link_libraries(Charm ${CHARM_LD_LIBS} ${CHARM_STATIC_LIBS} ${EXTRA_LIBS})
set_target_properties(Charm PROPERTIES
    VERSION 1.0
    SOVERSION 1
    PUBLIC_HEADER "charm.h"
)
