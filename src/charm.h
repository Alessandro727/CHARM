/* charm.h

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

#ifndef _CHARM_H_
#define _CHARM_H_

#ifdef NUMA_AWARE
#include <numa.h> 
#endif

#include "sched/barrier.h"
#include "sched/scheduler.h"
#include "comm/communicator.h"
#include "comm/message.h"
#include "comm/verbs.h"
#include "comm/completion.h"
#include "utils/utils.h"
#include "utils/memlog.h"
#include "pgas/addressing.h"
#include "pgas/allocator.h"
#include "pgas/garray.h"
#include "pgas/vmemory.h"
#include "pgas/collective.h"
#include "pgas/rpc.h"
#include "pgas/parallelfor.h"

#include "perf/perf_counter.h"

namespace Charm{

void CHARM_Init(int* argc, char** argv[]);

void CHARM_Finalize();

}//namespace Charm
#endif
