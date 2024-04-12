/* messagepool.cc

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

#include "comm/messagepool.h"

namespace Charm{


void MessagePool::init(int which_numa){
  for(int i = 0; i < MAX_POOL_CNT; ++i){
    size_t object_size = (i+1) * CACHE_LINE_SIZE;
    size_t preset_num = EACH_POOL_SIZE / object_size;

    // bigger than MAX_POOL_CUTOFF message will just allocate 1 chunk.
    if(i >= MAX_POOL_CACHELINE){
      preset_num = 1;
    }

    pools[i].init(object_size, preset_num, which_numa);
  }
}

void* MessagePool::alloc(size_t size){
  size_t sz = (size_t)ALIGN_UP(size, CACHE_LINE_SIZE);
  size_t cacheline_cnt = sz / CACHE_LINE_SIZE; // 53 / 64 == 0

  if( sz > MAX_MESSAGE_SIZE ){
    // use heap allocation
    return align_alloc<char>(CACHE_LINE_SIZE, sz);
    msg_on_heap ++ ;
  }else{
    switch(cacheline_cnt){
      case 1 : msg_1_cacheline ++;break;
      case 2 : msg_2_cacheline ++;break;
      case 3 : msg_3_cacheline ++;break;
      case 4 : msg_4_cacheline ++;break;
      default : msg_x_cacheline ++; break;
    }
    // use pool allocation
    return pools[cacheline_cnt].alloc();
  }
}

void MessagePool::free(Message* m, size_t size){
  size_t sz = (size_t)ALIGN_UP(size, CACHE_LINE_SIZE);
  size_t cacheline_cnt = sz / CACHE_LINE_SIZE; 

  if( sz > MAX_MESSAGE_SIZE){
    // use heap free (no reuse)
    ::free(m);
  }else{
    // use pool free (reuse)
    pools[cacheline_cnt].free(m);
  }
}

};//namespace Charm
