/* mpienv.cc

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

#include "comm/mpienv.h"

namespace Charm{

mpi_env_t* mpi_env;

/* build MPI env */
void mpi_env_t::mpi_env_init(int *argc, char** argv[]){
  int is_inited = 0;
  MPI_Initialized( &is_inited );
  if(is_inited) return;
  MPI_Init(argc, argv);
  comm = MPI_COMM_WORLD;
  MPI_Comm_rank( MPI_COMM_WORLD, &rank_ );
  MPI_Comm_size( MPI_COMM_WORLD, &size_ );
  cores_ = size * THREAD_SIZE;
  //cores_ = 8;

  // check that every node should have only one MPI process.
  MPI_Comm _tmp_comm;
  int num = 0;
  MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &_tmp_comm);
  MPI_Comm_size(_tmp_comm, &num);
  ASSERT( num==1, "Every node should have only one MPI process" );
}

void mpi_env_t::mpi_env_finalize(){
  int is_finalized = 0;
  MPI_Finalized( &is_finalized );
  if( is_finalized ) return;

  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
}

void mpi_env_t::barrier(){
  MPI_Barrier(comm);
}

mpi_env_t * get_mpi_env(){
  return mpi_env;
}

}//namespace Charm

