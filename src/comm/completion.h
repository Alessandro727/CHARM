/* completion.h

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

#ifndef _COMPLETION_H_
#define _COMPLETION_H_

#include "utils/utils.h"
#include "comm/message.h"
#include "comm/communicator.h"
#include "sched/barrier.h"
#include "comm/mpienv.h"
#include "sched/scheduler.h"

namespace Charm{

struct DoComplete {
  // :)
  Barrier *** barrier;
  int target;
  int64_t dec;
    
  DoComplete(): barrier(NULL),target(0),dec(0) {}
    
  void operator()() {
    (*barrier)[target]->complete(dec);
  }
};

class Completion: public Request<DoComplete>{
public:
  int target;
  int64_t completion_to_send;

  Completion(int target = -1)
    : Request(), target(target), completion_to_send(0){}
  

  // since message has enqueued,
  // put the completion in the waiting room and wait for next one;
  inline bool is_too_late(){
    return this->is_enqueued && (!this->is_sent);
  }

  //used in the pooling 
  inline void maybe_resend(){
    //ASSERT (my_id() == this->get_source_core(), "why resend is not on source core." );
    Message::flag_reset();
    // if we have some completions in the waiting room.
    // re-send a message right now
    if( completion_to_send > 0 ){
      // set DoComplete dec;
      this->get_rpc()->dec = completion_to_send;
      completion_to_send = 0;
      // enqueue this message again;
      global_comm->enqueue(this);
    }
  }

} ;//class Completion

extern Completion ** completion_msgs;

void init_completion_msgs();

Completion * get_completion_msg(size_t owner);
void send_completion(size_t owner, int64_t dec=1);

void poolling_completion();


};//namespace Charm
#endif
