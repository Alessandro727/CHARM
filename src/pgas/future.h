/* future.h
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

#ifndef _FUTURE_H_
#define _FUTURE_H_

#include "sched/coroutine.h"
#include "sched/scheduler.h"

namespace Charm{

template<typename T>
class Future{
  
public:
  Future(): done_tag(false) {}

  void set_value(T t) {
    _content = t;
    done_tag = true;
    auto w = global_scheduler->get_cur_worker();
    w->signal(&cv);
  }

  T get() {
    if(!done_tag){
      auto w = global_scheduler->get_cur_worker();
      w->wait(&cv);
    }
    return _content;
  }
private:
  bool done_tag;
  T _content;
  Condition cv;
};

}//namespace Charm
#endif
