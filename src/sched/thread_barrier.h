/* thread_barrier.h
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

#ifndef _THREAD_BARRIER_H_
#define _THREAD_BARRIER_H_

#include <thread>
#include <iostream>
#include <mutex>
#include <condition_variable>

namespace Charm{

/* just used to block entire thread */
class Thread_Barrier {
public:
  Thread_Barrier() : cnt_(0) {}
  explicit Thread_Barrier(int cnt) : cnt_(cnt){}

  inline void issue(int inc=1){
    std::lock_guard<std::mutex> lk(mutex_);
    cnt_ += inc;
  }

  inline void complete(){
    std::unique_lock<std::mutex> lk(mutex_);
    if( --cnt_ == 0 ){
      cv_.notify_all();
    }
  }

  inline void await(){
    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait(lk, [this]{return cnt_ == 0;});
  }

  inline int get_cnt(){
    std::unique_lock<std::mutex> lk(mutex_);
    return cnt_; 
  }


  inline void wait(){
    std::unique_lock<std::mutex> lk(mutex_);
    if( --cnt_ == 0 ){
      cv_.notify_all();
    }else{
      cv_.wait(lk, [this]{return cnt_ == 0;});
    }
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int cnt_;
};

}

#endif
