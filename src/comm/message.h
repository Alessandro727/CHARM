/* message.h

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

#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include <iostream>
#include <cstdio>
#include <cstring>

#include "sched/scheduler.h"

namespace Charm{

class Message{
public:
  Message *next;
  Message *prefetch;

  union{
    struct{
      bool is_sent : 1;
      bool is_enqueued : 1;
      bool is_delete_after_send : 1; 
      bool is_reuse : 1;
      //uint16_t from_id : 16;
      uint16_t to_id : 16;
    };
    uint32_t raw;
  };
  //size_t from_thread, to_thread;
  //size_t from_node, to_node;
  //bool is_sent;
  //bool is_enqueued;
  //bool is_delete_after_send;

  Message() : next(NULL), prefetch(NULL), raw(0) {}

  void flag_reset(){
    is_enqueued = false;
    is_sent = false;
    is_delete_after_send = false;
    is_reuse = false;
  }

  //uint16_t get_source_core(){
    //return from_id; 
  //}

  uint16_t get_destination_core(){
    return to_id;
  }

  virtual void mark_as_sent(){
    is_sent = true;
  }

  virtual void set_enqueued(){
    is_enqueued = true;
  }

  virtual void delete_after_send(bool flag = true){
    is_delete_after_send = flag;
  }

  virtual void set_reuse(){
    is_reuse = true;
  }

  void wait_for_send(){
    while(!is_sent){
      yield();
    }
  }

  inline static void deserialize_and_call( char * f ){};
  virtual ~Message(){};
  virtual void call()=0;
  virtual void delivery()=0;
  virtual size_t get_size()=0;
  virtual size_t size()=0;
  virtual size_t serialize_to(char *p)=0;
};


template< typename F >
class Request : public Message{
public:
  inline Request() : Message(), rpc() {}

  inline explicit Request( F f ): Message(), rpc( f ){}

  virtual ~Request(){
    wait_for_send(); 
  }

  inline void route(uint16_t from_id, uint16_t to_id){
    //this->from_id = from_id;
    this->to_id = to_id;
  }

  inline void route(uint16_t fn, uint16_t ft, uint16_t tn, uint16_t tt){
    //this->from_id = fn * THREAD_SIZE + ft;
    this->to_id = tn * THREAD_SIZE + tt;
  }
  
  void call(){
    rpc();
  }

  //TODO: delivery cost time in free phase, maybe freeing lambda is the bottleneck.
  void delivery(){
    ASSERT((int)(to_id/THREAD_SIZE) == mpi_env->rank, "delivery Message to wrong node.");
    ASSERT(false, "too bad");
    //global_scheduler->private_enqueue( to_id%THREAD_SIZE, rpc);
  }

  inline F* get_rpc(){
    return &rpc;
  }

  static char* deserialize_and_call( char * f ){
    F * obj = reinterpret_cast< F * >( f );
    (*obj)();
    return f + sizeof(F);
  }

  inline F deserialize(char * f){
    return (*reinterpret_cast<F*>( f ));
  }

  inline size_t get_size(){
    return sizeof(&deserialize_and_call) + sizeof(rpc);
  }

  inline size_t size(){
    return sizeof(*this);
  }

  inline size_t serialize_to( char *p ){
    using Des=char*(*)(char *);
    int cnt = 0;
    *(reinterpret_cast<Des *>(p)) = &deserialize_and_call;
    char * xp = p + sizeof( &deserialize_and_call );
    cnt += sizeof( &deserialize_and_call );
    memcpy( xp, &rpc, sizeof(rpc) );
    cnt += sizeof( rpc );
    return cnt;
  }

private:
  F rpc;
};

template< typename T, typename F >
class Request_with_payload : public Message{
public:
  inline explicit Request_with_payload( std::vector<T> data,  F f )
    : Message(), payload(std::move(data)), rpc(f) {}

  virtual ~Request_with_payload(){
    wait_for_send(); 
  }

  inline void route(uint16_t from_id, uint16_t to_id){
    //this->from_id = from_id;
    this->to_id = to_id;
  }

  inline void route(uint16_t fn, uint16_t ft, uint16_t tn, uint16_t tt){
    //this->from_id = fn * THREAD_SIZE + ft;
    this->to_id = tn * THREAD_SIZE + tt;
  }

  void call(){
    rpc( std::move(payload) );
  }

  static void deserialize_and_call( char * f ){
    /* read rpc */
    F * obj = reinterpret_cast< F * >( f );
    f += sizeof(F);
    /* read payload */
    int sz = *(reinterpret_cast<int*>(f));
    f += sizeof(int);
    std::vector<T> data; 
    data.resize(sz);
    for(int i = 0; i < sz; ++i ){
      data[i] = *(reinterpret_cast<T*>(f));
      f += sizeof(T);
    }
    /* do it */
    (*obj)( std::move(data) );
  }

  inline F deserialize(char * f){
    return (*reinterpret_cast<F*>( f ));
  }

  inline size_t get_size(){
    return sizeof(&deserialize_and_call) + sizeof(rpc) + sizeof(int) + sizeof(T)*payload.size();
  }

  inline size_t size(){
    return sizeof(*this);
  }


  inline size_t serialize_to( char *p ){
    using Des=void (*)(char *);
    int cnt = 0;
    /* deserialize */
    *(reinterpret_cast<Des *>(p)) = &deserialize_and_call;
    char * xp = p + sizeof( &deserialize_and_call );
    cnt += sizeof( &deserialize_and_call );
    /* lambda closure */
    memcpy( xp, &rpc, sizeof(rpc) );
    xp += sizeof(rpc);
    cnt += sizeof( rpc );
    /* payload size */
    *(reinterpret_cast<int*>(xp)) = payload.size();
    char * payload_base =  xp + sizeof(rpc);
    cnt += sizeof(int);
    /* payload */
    for(auto x : payload){
      memcpy(payload_base, &x, sizeof(x));
      payload_base += sizeof(x);
      cnt += sizeof(x);
    }
    return cnt;
  }

private:
  F rpc;
  std::vector<T> payload;
};

}//namespace Charm

#endif
