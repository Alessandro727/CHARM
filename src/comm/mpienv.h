/* mpienv.h

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

#ifndef _MPIENV_H_
#define _MPIENV_H_

#include <cstdint>
#include <iostream>

#include <mpi.h>

#include "utils/utils.h"

namespace Charm{


class mpi_env_t {
public:
  mpi_env_t() : rank(rank_), size(size_), cores(cores_){}
  void mpi_env_init(int *argc, char** argv[]);
  void mpi_env_finalize();
  void barrier();
  const int& rank;
  const int& size;
  const int& cores;
  inline MPI_Comm get_comm() { return comm; }
private:
	int rank_;
	int size_;
  int cores_;
	MPI_Comm comm;
};

extern mpi_env_t* mpi_env;

mpi_env_t * get_mpi_env();

inline const int node_rank(){
  return mpi_env->rank;
}

inline const int node_size(){
  return mpi_env->size;
}

inline const int cores(){
  return mpi_env->cores;
}

}//namespace Charm

#endif
