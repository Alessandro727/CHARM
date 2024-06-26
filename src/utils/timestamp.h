/* timestamp.h
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

#ifndef _TIMESTAMP_H_
#define _TIMESTAMP_H_

#include <cstdint>
#include <atomic>

#include "utils/utils.h"

namespace Charm{

class Timestamp{
  const int64_t DFT_CNT = 1;
public:
  inline uint64_t get_stamp(){
    if( cnt_-- < 0 ){
      cur_stamp_ = rdtsc();
      cnt_ = DFT_CNT;
    }
    return cur_stamp_;
  }

  inline uint64_t get_stamp_no_share(){
    return rdtsc();
  }

  inline uint64_t get_stamp_high_overhead(){
    return std::chrono::duration_cast<std::chrono::milliseconds>
      (std::chrono::system_clock::now().time_since_epoch()).count();
  }


  Timestamp() : cnt_(DFT_CNT){
    cur_stamp_ = rdtsc();
  }
  
private:
  int64_t cnt_;
  uint64_t cur_stamp_;
};

extern thread_local Timestamp grdtsc_reg;

inline uint64_t get_tick(){
#ifdef FASTCS
  return grdtsc_reg.get_stamp_no_share();
  //return grdtsc_reg.get_stamp();
#else
  return grdtsc_reg.get_stamp_high_overhead();
#endif
  //return grdtsc_reg->get_stamp();
}

}//namespace Charm

#endif
