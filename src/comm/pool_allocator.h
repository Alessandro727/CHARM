/* pool_allocator.h

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

#ifndef _POOL_ALLOCATOR_H_
#define _POOL_ALLOCATOR_H_

#include <cstdint>
#include <malloc.h>


#include "utils/utils.h"

namespace Charm{

// align ptr x to y boundary (rounding up)
#define ALIGN_UP(x, y) \
    ((((u_int64_t)(x) & (((u_int64_t)(y))-1)) != 0) ? \
        ((void *)(((u_int64_t)(x) & (~(u_int64_t)((y)-1)))+(y))) \
        : ((void *)(x)))

struct memory_chunk_t{
  struct memory_chunk_t* next;
  char* chunk;
  size_t chunk_size;
  size_t offset;
};

struct link_object_t{
  struct link_object_t* next;
};

class ChunkAllocator{
public:
  ChunkAllocator() : total_allocated(0){}; 
  void init(size_t chunk_size, int which_numa);
  void alloc_new_chunk(size_t size);
  void* alloc(size_t size);
  void free(void* p);

  size_t total_allocated;
private:
  int which_numa;
  size_t chunk_size;
  struct memory_chunk_t* first;
  struct memory_chunk_t* last;
};


class PoolAllocator{
public:
  PoolAllocator(){}
  void init(size_t object_size, size_t preset_num, int which_numa);
  void* alloc();
  void free(void* p);

private:
  ChunkAllocator chunk_allocator;
  size_t object_size;
  size_t preset_num;
  int index;
  struct link_object_t* prefetch[POOL_PREFETCH_DIST];
  //size_t allocated_cnt;
  //size_t fall_back_allocated_cnt;
};

#ifdef NUMA_AWARE
template<typename T>
T* numa_alloc(int which_numa, size_t num=1){
  void* tmp = numa_alloc_onnode(sizeof(T)*num, which_numa);
//  void* tmp = malloc(sizeof(T)*num);
  return (T*)tmp;
}
#endif

template<typename T>
T* align_alloc(size_t align, size_t num=1){
  void * tmp;
  posix_memalign(&tmp, align, sizeof(T)*(num));
  return (T*)tmp;
}


};//namespace Charm

#endif
