/* task.h
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

#ifndef _TASK_H_
#define _TASK_H_

#include <utils/utils.h>

namespace Charm{

struct arg_pack_t{
  void *arg0, *arg1, *arg2;
};

static inline struct arg_pack_t 
pack_args(void *arg0, void *arg1, void* arg2){
  arg_pack_t w;
  w.arg0 = arg0;
  w.arg1 = arg1;
  w.arg2 = arg2;
  return w;
}

//Try Grappa 32-bytes fix size task hack
class Task{
public:
  Task(){}
  Task(void (*f)(void*,void*,void*), arg_pack_t ap)
    : f(f), ap(ap){}
  
  void operator()(){
    f(ap.arg0, ap.arg1, ap.arg2);
  }

private:
  void(*f)(void*,void*,void*);
  arg_pack_t ap;
};

template <typename A0, typename A1, typename A2>
Task createTask(void (*f)(A0,A1,A2), A0 arg0, A1 arg1, A2 arg2){
  arg_pack_t ap = pack_args((void*)arg0, (void*)arg1, (void*)arg2);
  Task t(reinterpret_cast<void(*)(void*,void*,void*)>(f), ap);
  return t;
}


};//namespace Charm



#endif
