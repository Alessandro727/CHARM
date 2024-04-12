/* barrier.h -- coroutine barrier
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

#ifndef _BARRIER_H_
#define _BARRIER_H_

#include <mutex>
#include <atomic>

#include "sched/coroutine.h"
#include "sched/scheduler.h"

namespace Charm{

/* this Barrier can be reused */
class Barrier : public Condition{
public:
  Barrier() : cnt_(0){}
  Barrier(int64_t num) : cnt_(num) {}

  inline void issue(int inc = 1){
    cnt_ += inc;
  }

  inline void complete(int dec = 1){
    cnt_ -= dec;
    if( cnt_ == 0 ){
      auto w = global_scheduler->get_cur_worker();
      while( !this->empty() ){
        w->signal(this);
      }
    }
  }

  inline void wait(){
    auto w = global_scheduler->get_cur_worker();

    if( cnt_ == 0 ){
      while( !this->empty() ){
        w->signal(this);
      }
    }else{
      w->wait(this);
    }
  }

private:
  int64_t cnt_;
};

extern Barrier* global_barrier;
extern Barrier** call_inflight;
extern Barrier locale_barrier;

}//namespace Charm

#endif
