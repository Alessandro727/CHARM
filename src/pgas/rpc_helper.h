/* rpc_helper.h
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

#ifndef _RPC_HELPER_H_
#define _RPC_HELPER_H_

#include "sched/scheduler.h"
#include "comm/communicator.h"
#include "comm/completion.h"
#include "pgas/future.h"
#include "pgas/addressing.h"

namespace Charm{

// blocking without return value
template<typename F>
void rcall(size_t id, F func, void (F::*mf)() const){
  size_t from_id = my_id();
  size_t to_id = id;

  if(from_id == to_id){
    func(); 
  }else{
    Future<bool> fu;
    auto gfu = make_global(&fu);
    send_request(to_id, [func, gfu]{
      func();
      /* send complete msg */
      send_request(gfu.get_id(), [gfu]{ 
        auto pfu = gfu.ptr();
        pfu->set_value(true);
      });
    });
    // block
    fu.get();
  }
}

// blocking with return value
template<typename F, typename T>
auto rcall(size_t id, F func, T (F::*mf)() const) -> T{
  size_t from_id = my_id();
  size_t to_id = id;

  if(from_id == to_id){
    return func(); 
  }else{
    Future<T> fu;
    auto gfu = make_global(&fu);
    send_request(to_id, [func, gfu]{
      T ans = func();
      /* send complete msg */
      send_request(gfu.get_id(), [ans, gfu]{ 
        auto pfu = gfu.ptr();
        pfu->set_value(ans);
      });
    });
    // block
    T ans = fu.get();
    return ans;
  }
}

template<typename F, typename T>
auto fcall(size_t id, F func, T (F::*mf)()const) -> std::unique_ptr<Future<T>>{
  size_t from_id = my_id();
  size_t to_id = id;

  //TODO: heap allocation is bad :(
  std::unique_ptr<Future<T>> fu(new Future<T>());
  auto pfu = fu.get();

  if(from_id == to_id){
    T ans = func();
    pfu->set_value(ans);
  }else{
    auto gpfu = make_global(&pfu);
    send_request(to_id, [func, gpfu]{
      T ans = func();
      /* send complete msg */
      send_request(gpfu.get_id(), [ans, gpfu]{ 
        auto pfu = gpfu.ptr();
        pfu->set_value(ans);
      });
    });
  }
  return fu;
}

};//namespace Charm


#endif
