/* pool_allocator.cc

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

#include <cstdlib>

#include "comm/pool_allocator.h"
#include "sched/scheduler.h"

thread_local bool warning_flag = true;

namespace Charm {

void ChunkAllocator::init(size_t chunk_size, int which_numa){
  this->chunk_size = chunk_size;
  this->first = this->last = NULL;
  this->which_numa = which_numa;
}

void ChunkAllocator::alloc_new_chunk(size_t size){
  struct memory_chunk_t *new_chunk;
  new_chunk = align_alloc<struct memory_chunk_t>(CACHE_LINE_SIZE);
  
  new_chunk->next = NULL;
  new_chunk->chunk_size = this->chunk_size;
  new_chunk->chunk = align_alloc<char>(CACHE_LINE_SIZE, new_chunk->chunk_size);
  total_allocated += chunk_size;

  if(!first){
    last = first = new_chunk;   
  }else{
    last->next = new_chunk;
    last = new_chunk;
    last->next = NULL;
  }
  last->offset = 0;
}

void* ChunkAllocator::alloc(size_t size){
  size = (size_t)ALIGN_UP(size, 64);

  ASSERT(chunk_size >= size, "overflow pool total size");
  if(this->last == NULL || this->last->chunk_size - this->last->offset < size){
    alloc_new_chunk(size);
  }
    
  void *p = &last->chunk[last->offset];
  last->offset += size;
  return p;
}

void ChunkAllocator::free(void* p){
  // do nothing
}

void PoolAllocator::init(size_t object_size, size_t preset_num, int which_numa){
  this->object_size = object_size;
  this->preset_num = preset_num;
  this->index = 0;

  chunk_allocator.init(object_size*preset_num, which_numa);

  for(int i = 0 ; i < POOL_PREFETCH_DIST; ++i){
    this->prefetch[i] = NULL;
  }
}

void* PoolAllocator::alloc(){
  struct link_object_t * ret;
#ifdef PREFETCH
  __builtin_prefetch(prefetch[0], 0, 0);
#endif

  int yield_cnt = 0;
  do{
    // 1. try default list
    ret = prefetch[index];

    // 2. search for a newly freed one
    if( !ret ){
      int i=0;

      while(i < POOL_PREFETCH_DIST){
        if(prefetch[i]) break;
        else ++i;
      }

      if(i < POOL_PREFETCH_DIST){
        index = i;
        ret = prefetch[index];
      }
    }

    if( !ret ){
      // 3. try allocate it myself
      if( chunk_allocator.total_allocated < POOL_MAX_SIZE){
        ret = (struct link_object_t *) chunk_allocator.alloc(object_size);
        //if(my_id() == 0) std::cout << chunk_allocator.total_allocated << " " << POOL_MAX_SIZE << std::endl;
        return ret;
      }else{
        if(warning_flag){
          warning_flag = false;
          std::cout << "Warning : message pools used up, Performance hurts!" << std::endl;
        }
        // 4. wait for other coroutine free one
        // TODO: sometimes dead loop here.
        //sp_cnt++;
        yield_cnt ++;
        if(yield_cnt == 100000){
          std::cout << "dead loop ok?" << std::endl;
          yield_cnt = 0;
        }

        yield();
      }
    }

  }while(!ret);

  // next time allocate from next prefetch item
#ifdef PREFETCH
  __builtin_prefetch(prefetch[index], 0, 0);
#endif
  ret = prefetch[index];
  prefetch[index] = ret->next;
#ifdef PREFETCH
  if(prefetch[index])
    __builtin_prefetch(prefetch[index], 0, 3);
#endif
  index = (index + 1) % POOL_PREFETCH_DIST;
  ret->next = NULL;
  return (void*)ret;
}

void PoolAllocator::free(void* p){
  struct link_object_t *nextpf = (struct link_object_t*) p;

  nextpf->next = prefetch[index];
  prefetch[index] = nextpf;
  index = (index + 1) % POOL_PREFETCH_DIST;
}


};//namespace Charm
