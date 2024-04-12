/* vmemory.cc
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

#include <malloc.h>

#include "pgas/vmemory.h"
#include "utils/utils.h"


namespace Charm{


Vmemory::Vmemory(size_t total_shared_size)
  : size_per_core_( round_up_page_size( total_shared_size/ (node_size()*THREAD_SIZE) ) )
  , size_per_node_( size_per_core_ * THREAD_SIZE )
  , my_pm_base_( size_per_core_ )
  , allocator_( size_per_node_ * node_size(), true){
    partition_mem_base = (intptr_t*)malloc( (int64_t)THREAD_SIZE*sizeof(intptr_t) );
    ASSERT(partition_mem_base != NULL, "malloc pgas memory failed!");
    for(size_t i = 0; i < THREAD_SIZE;  ++i)
      partition_mem_base[i] = get_base(i); 
  }


Vmemory* global_mem_manager;

intptr_t* partition_mem_base;

}//namespace Charm
