/* messagepool.h

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

#ifndef _MESSAGEPOOL_H_
#define _MESSAGEPOOL_H_

#include <mutex>
#include <thread>

#include "comm/pool_allocator.h"
#include "comm/message.h"
#include "sched/scheduler.h"

namespace Charm {

/* TODO: we can't reuse this MessagePool right now
 * since it will increase the system complexity */
const int MAX_MESSAGE_SIZE = 1<<10;
const int MAX_POOL_CNT = MAX_MESSAGE_SIZE / CACHE_LINE_SIZE;
const int MAX_POOL_CUTOFF = 1<<8;
const int MAX_POOL_CACHELINE = MAX_POOL_CUTOFF / CACHE_LINE_SIZE;
const int EACH_POOL_SIZE = 1<<15;

class MessagePool{
public:
  void init(int which_numa);
  void* alloc(size_t size);
  void free(Message* m, size_t size);
private:
  PoolAllocator pools[MAX_POOL_CNT];
} CACHE_ALIGNED;


}//namespace Charm



#endif
