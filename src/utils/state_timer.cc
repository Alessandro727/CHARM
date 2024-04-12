/* state_timer.cc
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * INSTITUTE OF COMPUTING TECHNOLOGY AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <iostream>
#include <fstream>
#include <cstdio>

#include <mpi.h>

#include "utils/state_timer.h"

namespace Charm{

Thread_Timer * ttimer;

TIME get_time(){
  return MPI_Wtime();
}

void Thread_Timer::stage_init(int thread_rank){
  do_trace_ = 0;
  history_.clear();
  thread_rank_ = thread_rank;
}

void Thread_Timer::stage_start(){
  cur_rec_.reset();
  last_stage_ = Stage(-1);
  do_trace_ = 1;
}

void Thread_Timer::stage_stop(){
  if(do_trace_) stage_switch_to( Stage(-1) );
  do_trace_ = 0;
  history_.push_back( std::move(cur_rec_) );
}

void Thread_Timer::stage_switch_to(enum Stage cur_stage){
  if(cur_stage == last_stage_) return;
  TIME cur_stamp = get_time();
  if( do_trace_ ){
    TIME addon = cur_stamp-last_stamp_;
    switch(last_stage_){
      case Task_worker: {cur_rec_.task += addon; break;}
      case Send_worker: {cur_rec_.send += addon; break;}
      case Recv_worker: {cur_rec_.recv += addon; break;}
      case Polling_worker: {cur_rec_.polling += addon; break;}
      case Peer_worker: {cur_rec_.peer += addon; break;}
      default : break;
    }
    last_stage_ = cur_stage;
    last_stamp_ = cur_stamp;
  }
}

void Thread_Timer::stage_dump(){
  char fn[64];
  sprintf(fn, "thread_profile_%d", thread_rank_);
  std::ofstream orz(fn, std::ios::out);
  for(auto r: history_){
    orz << r.task << " "
        << r.send << " "
        << r.recv << " "
        << r.polling << " "
        << r.peer << std::endl;
  }
  orz.close();
}

}//namespace Charm

