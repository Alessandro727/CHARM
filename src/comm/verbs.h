/* verbs.h

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

#ifndef _VERBS_H_
#define _VERBS_H_

#include <iostream>
#include <cstdint>
#include <vector>

#include <infiniband/verbs.h>

#include "comm/mpienv.h"
#include "utils/utils.h"

namespace Charm {


struct remote_qp_attr_t {
  // Info about RDMA buffer associated with this QP
#ifdef NUMA_BUF
  uintptr_t rdma_buf[SOCKETS];
  uint32_t rdma_buf_size[SOCKETS];
  uint32_t rkey[SOCKETS];
#else
  uintptr_t rdma_buf;
  uint32_t rdma_buf_size;
  uint32_t rkey;
#endif
  // extra parameters needs to be exchanged when connetting QPs
  uint16_t lid;
  uint32_t qpn;
};

struct RDMA_pack_t{
  uint64_t addr;
  size_t size;
  size_t offset;
  RDMA_pack_t(uint64_t _addr, size_t _size, size_t _offset) 
    : addr(_addr), size(_size), offset(_offset){}
};





class C_Ctl;

class Verbs{
public:
  Verbs();

#ifdef NUMA_BUF
  void activate(int c_ctl_size,  uint64_t*** rm_recv_buf);
  void pin_buffer(int which_numa, char* buf, int64_t size);
#else
  void activate(int c_ctl_size,  uint64_t** rm_recv_buf);
  void pin_buffer(char * buf, int64_t size);
#endif
  int get_rank();
  int get_size();

  void init_device(const char* target_device_name="mlx4_0", 
                   uint8_t target_port=1);
  void finalize();
   
  /* RDMA verbs C_Ctl */
#ifdef NUMA_BUF
  void post_RDMA_write_single(int rank, int which_numa, uint8_t* msg, int size, int offset);
  void post_RDMA_write_batch(int rank, int which_numa, std::vector<struct RDMA_pack_t> pack);
  void post_RDMA_write_raw(int rank, int which_numa, uint32_t inline_data, uint32_t* target_addr);
  void poll_cq(int rank, int which_numa, int num);
#else
  void post_RDMA_write_single(int rank, uint8_t* msg, int size, int offset);
  void post_RDMA_write_batch(int rank, std::vector<struct RDMA_pack_t> pack);
  void post_RDMA_write_raw(int rank, uint32_t inline_data, uint32_t* target_addr);
  void poll_cq(int rank, int num);
#endif

  /* TODO msg verbs UD_Ctl */
  // 
  
private:
  /* device */
  ibv_device * device;
  const char * device_name;
  uint64_t device_guid;
  ibv_device_attr device_attr;

  /* port */
  uint8_t port;
  ibv_port_attr port_attr;

  /* context && protection_domain */
  ibv_context * context;
  ibv_pd * protection_domain;

  /* pinned_buffer*/
#ifdef NUMA_BUF
  struct ibv_mr* pinned_buffer[SOCKETS];
#else
  struct ibv_mr* pinned_buffer;
#endif

  /* QP ctl */
  int c_ctl_size;
  C_Ctl** c_ctl;
};

class C_Ctl{
  /* config */
  const uint32_t DFT_Q_DEPTH = 1<<13;
  const uint32_t DFT_MAX_INLINE = 64;
  const uint32_t DFT_PSN = 0;
  const uint32_t DFT_MAX_DEST_RD_ATOMIC = 16;
  const uint32_t DFT_MAX_RD_ATOMIC = 16;
  const uint32_t DFT_MIN_RNR_TIMER = 0x12;
  const uint32_t DFT_TIMEOUT = 14;
  const uint32_t DFT_RETRY_CNT = 7;
  const uint32_t DFT_RNR_RETRY = 7;

public:
  C_Ctl()=delete;
  C_Ctl(struct ibv_context* ctx, struct ibv_pd* pd, 
        uint8_t port, struct ibv_port_attr port_attr) 
    : context(ctx), protection_domain(pd), 
      port(port), port_attr(port_attr){}
  
#ifdef NUMA_BUF
  void init(int num_conn_qps, uint32_t* lkey, uint32_t* rkey, uint64_t** rm_recv_buf);
#else
  void init(int num_conn_qps, uint32_t lkey, uint32_t rkey,uint64_t* rm_recv_buf);
#endif

  void finalize();

#ifdef NUMA_BUF
  void post_RDMA_write_single(int rank, int which_numa, uint8_t* msg, int size, int offset, uint32_t lkey);
  bool post_RDMA_write_batch(int rank, int which_numa, const std::vector<struct RDMA_pack_t>& pack, uint32_t lkey);
  bool post_RDMA_write_raw(int rank, int which_numa, uint64_t inline_data, uint32_t* target_addr, uint32_t lkey);
#else
  void post_RDMA_write_single(int rank, uint8_t* msg, int size, int offset, uint32_t lkey);
  bool post_RDMA_write_batch(int rank, const std::vector<struct RDMA_pack_t>& pack, uint32_t lkey);
  bool post_RDMA_write_raw(int rank, uint64_t inline_data, uint32_t* target_addr, uint32_t lkey);
#endif
  bool poll_cq(int rank, int num);


private:
  /* context and protection domain*/
  struct ibv_context * context;
  struct ibv_pd * protection_domain;
  uint8_t port;
  ibv_port_attr port_attr;

  /* conn_qp */
  int num_qps;
  struct ibv_cq ** cq;
  struct ibv_qp ** qp;
#ifdef NUMA_BUF
  uint32_t lkey[SOCKETS], rkey[SOCKETS];
#else
  uint32_t lkey, rkey;
#endif

  /* remote qp info */
  int num_remote_qps;
  struct remote_qp_attr_t* remote_qps;

  void _create_qps();
#ifdef NUMA_BUF
  void _exchange_remote_qp_info(uint64_t** rm_recv_buf);
#else
  void _exchange_remote_qp_info(uint64_t* rm_recv_buf);
#endif
  void __connect_qps(int i, struct remote_qp_attr_t *remote_qp_attr);
  void _connect_qps();
};

extern Verbs* global_verbs;

}//namespace Charm
#endif

