/* charm.cc

Copyright (c) 2024 Alessandro Fogli

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "charm.h"

namespace Charm{

void CHARM_Init(int* argc, char** argv[]){
  /* bring up MPI environment */
  mpi_env = new mpi_env_t();
  mpi_env->mpi_env_init(argc, argv);

#ifdef NUMA_AWARE
  ASSERT( numa_available() >= 0, "System does not support Numa API.");
#endif

  config_available_memory();

  /* PGAS */
  global_mem_manager = new Vmemory(SHARED_MEMORY_SIZE);

  /* open infiniband device */
//  global_verbs = new Verbs();

  /* get a memory and pin it */
#ifdef NUMA_BUF
  int64_t per_socket_size = COMM_MEMORY_SIZE/SOCKETS;
  for(int i = 0; i < SOCKETS; ++i){
    global_allocators[i] = new Allocator(i, per_socket_size, false);
//    global_verbs->pin_buffer(i, (char *)global_allocators[i]->get_base(), per_socket_size);
  }
#else
  global_allocator = new Allocator( COMM_MEMORY_SIZE, false);
//  global_verbs->pin_buffer((char *)global_allocator->get_base(), COMM_MEMORY_SIZE);
#endif
  /* build scheduler */
  /* add task worker */
  global_scheduler = new Scheduler( INIT_CORO_NUM );

  /* build Communicator */
  global_comm = new Communicator();
  global_barrier = new Barrier();
  size_t num = global_scheduler->get_size();
  call_inflight = (Barrier**)malloc( num*sizeof(Barrier*) );
  for(size_t i = 0; i < num; ++i) call_inflight[i] = new Barrier();
  init_completion_msgs();

  /* activate verbs */
  /*TODO: the API is really ugly */
  auto my_recv_buf = global_comm->get_my_recv_buf();
  //global_verbs->activate(2, &my_recv_buf);

  /* activate communicator */
  global_exit_flag = false;
  global_comm->activate();

  running = false;
  mpi_env->barrier();
  global_scheduler->start();
//#ifdef PROFILING
  //if(mpi_env->get_rank() == 0) ProfilerStart("ring.prof");
//#endif
  //std::cout << "--init--" << std::endl;
}

void CHARM_Finalize(){
  if(!running) done();
  global_scheduler->await();
  //if(mpi_env->rank == 0) log_dump();
//#ifdef PROFILING
  //if(mpi_env->get_rank() == 0) ProfilerStop();
//#endif
  mpi_env->mpi_env_finalize();
}

};//namespace Charm;
