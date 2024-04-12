/* completion.cc

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

#include "comm/completion.h"

namespace Charm{

Completion ** completion_msgs;

void init_completion_msgs(){
  size_t from_node = mpi_env->rank;
  size_t cores = mpi_env->cores;

  completion_msgs = (Completion**)malloc( cores * THREAD_SIZE * sizeof(Completion*) );

  for(size_t i = 0; i < THREAD_SIZE; ++i){
    for(size_t j = 0; j < cores; ++j){

      auto x = new Completion(j);
      // you know, this pointer on all the node must be the same.
      x->get_rpc()->barrier = &call_inflight;
      x->get_rpc()->target = j%THREAD_SIZE;
      x->route( from_node, i, j/THREAD_SIZE, j%THREAD_SIZE );

      completion_msgs[ i*cores + j ] = x;
    }
  }
}

Completion* get_completion_msg(size_t owner){
  return completion_msgs[ thread_rank() * mpi_env->cores + owner ];
}

#ifdef COALESCE
void send_completion(size_t owner, int64_t dec){
  if(owner == my_id()){
    call_inflight[thread_rank()]->complete(dec);
  }else{
    Completion * cm = get_completion_msg(owner);
    if(cm->is_too_late()){
      cm->completion_to_send +=dec;
      msg_coalesce_cnts++;
    }else{
      // no one has issued a Completion message
      // so we will do this
      cm->completion_to_send +=dec;
      cm->maybe_resend();
    }
  }
}
#else
void send_completion(size_t owner, int64_t dec){
  if(owner == my_id()){
    call_inflight[thread_rank()]->complete(dec);
  }else{
    int tr = owner%THREAD_SIZE;
    if(dec == 1){
      send_request(owner,[tr]{
        call_inflight[tr]->complete(1);
      });
    }else{
      send_request(owner, [tr,dec]{
        call_inflight[tr]->complete(dec);
      });
    }
  }
}
#endif

#ifdef COALESCE
void poolling_completion(){
  size_t cores = mpi_env->cores;
  for(size_t i = 0; i < cores; ++i){
    Completion * cm = get_completion_msg(i);
    if( !cm->is_too_late() ){
      cm->maybe_resend();
    }
  }
}
#else
void poolling_completion(){
  // do nothing
}
#endif

};//namespace Charm
