/* main.cc

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

#include <iostream>
#include <vector>
#include <cstdio>
#include <random>

#ifdef PROFILING
#include <google/profiler.h>
#endif

#include "charm.h"

int x=0;
thread_local int per_core=0;


// run with arbitrary node
void test1(){
  using namespace Charm;
  run([]{
    double w = 1.2;
    sync_printf("hello world", w);
  });//run
}

// run with arbitrary node
void test2(){
  using namespace Charm;
  run([]{
    all_do([]{
      sync_printf("hello I'am", id());
    });//all_do
  });//run
}

// run with >3 nodes
void test3(){
  using namespace Charm;
  run([]{
    all_do([]{
      if( id() == 0 ){
        auto gx = make_global(0, 2, &x);
        for(int i = 0 ; i < 10000 ; i ++){
          call(gx, [](int *x){
            *x = 9; 
            sync_printf(node_rank(), thread_rank(), (*x));
          });//call
        }
      }
    });//all_do
    //sync_printf("over");
  });//run
}

// run with >3 nodes
void test4(){
  using namespace Charm;
  run([]{
    all_do([]{
      if( id() == 0 ){
        auto gx = make_global(0, 0, &x);
        for(int i = 0 ; i < 10000 ; i ++){
          auto w = call(gx, [i](int *x){
            *x = i;    
            return *x;
          });//call
          sync_printf(w);
        }//for
      }//if
    });//all_do
  });//run
}

// run with >3 nodes
void test5(){
  using namespace Charm;
  run([]{
    all_do([]{
      if( id() == 0 ){
        size_t n = node_size()*thread_size();
        int w = 10000;
        while(w--){
          for(size_t i = 0 ; i < n ; ++i){
            call<async>(i, [i](){
              sync_printf(i);
            });//call
          }//for
        }//while
      }//if
    });//all_do
  });//run
}

// run with >3 nodes
void test6(){
  using namespace Charm;
  run([]{
    all_do([]{
      if( id() == 0 ){
        size_t n = node_size()*thread_size();
        int w = 10000;
        while(w--){
          for(size_t i = 0 ; i < n ; ++i){
            auto w = call<async>(i, [i](){
              return i;
            });//call
            auto k = w->get();
            sync_printf(k);
          }//for
        }//while
      }//if
    });//all_do
  });//run
}

// run with >3 nodes
void test7(){
  using namespace Charm;
  run([]{
    auto x = gmalloc<int>(48);
    auto x1 = x+1;
    call<async>(x1, [](int * w){
      sync_printf("hello pgas", id());
    });
    /* free request will be executed after rpc is executed. */
    gfree(x);
  });
}

// run with >3 nodes
void test8(){
  using namespace Charm;
  run([]{
    auto x = gmalloc<int>(1<<20);
    auto y = gmalloc<int>(1<<20);
    auto x1 = x + 230;
    auto y1 = y + 333;
    call<async>(x1, [](int * w){
      *w = 1;
      sync_printf("hello pgas x", *w, id());
    });

    call<async>(y1, [](int * w){
      sync_printf("hello pgas y", id());
    });
    /* free request will be executed after rpc is executed. */
    gfree(x);
    gfree(y);
  });

}

// run with >3 nodes
void test9(){
  using namespace Charm;
  run([]{
    all_do([]{
      if( id() == 0 ){
        size_t n = node_size()*thread_size();
        for(size_t i = 0 ; i < n ; ++i){
          call<async>(i, [i]{
              sync_printf("have send to",i);
              call<async>(0, [i]{
                sync_printf("have send to 0");
              });
          });//call
        }//for
      }//if
    });//all_do
  });//run
}

void test10(){
  using namespace Charm;
  run([]{
    auto x = gmalloc<uint64_t>(128);
    for(int i = 0; i < 128; ++i){
      sync_printf("[", i, ", ", (x+i).get_id(), "]");
    }
    gfree(x);
  });//run
}

void test11(){
  using namespace Charm;
  run([]{
    const int SZ = 1<<20;
    auto x = gmalloc<uint64_t>(SZ);
    pfor(x, SZ, [](int64_t idx, uint64_t* i){
      int a = node_rank()*THREAD_SIZE + thread_rank();
      //if(a >= 12) sync_printf(a);
      call<async>(0, [a]{
        sync_printf(a);
      });//call
    });//pfor
    gfree(x);
  });//run
}

void test12(){
  using namespace Charm;
  run([]{
      struct test_t{
        int value;
      } CACHE_ALIGNED;
      
      auto g = symm_gmalloc<struct test_t>();
      all_do([g]{
        g->value = my_id();
      });

      all_do([g]{
        sync_printf(g->value);
      });

  });
}

void test13(){
  using namespace Charm;
  run([]{
    struct test_t{
      int value;
    } CACHE_ALIGNED;

    auto g = symm_gmalloc<struct test_t>();
    all_do([g]{
      g->value = my_id();
    });

    all_do([g]{
      int x = allreduce<int, collective_add>(g->value);
      sync_printf(x);
    });

  });
}

void test14(){
  using namespace Charm;
  run([]{
    auto array = gmalloc<uint64_t>(96);
    pfor(array, 96, [array](int64_t i, uint64_t *w){
      sync_printf( i, make_linear(w)-array, my_id() );
    });
  });
}

void test15(){
  using namespace Charm;
  run([]{
    auto array = gmalloc<uint64_t>(96);
    all_do([array]{
      for (auto& v : iterate_local(array,96)) {
        sync_printf( make_linear(&v) - array, my_id() );
      }
    });
    memset(array, 12, 96);
    pfor(array, 96, [](uint64_t *w){
      sync_printf(my_id(), *w);
    });
    sync_printf("@@@@@@@@@@@@");
    all_do([array]{
      for (auto& v : iterate_local(array,96)) {
        v++;
      }
    });
    sync_printf("############");
    pfor(array, 96, [](uint64_t *w){
      sync_printf(my_id(), *w, w);
    });
  });
}

void test16(){
  using namespace Charm;
  run([]{
    all_do([]{
      per_core = my_id();
      sync_printf(&per_core);
    });
    all_do([]{
      sync_printf(per_core);
    });
  });
}


void naive(){
  using namespace Charm;
  const size_t SZ = 1<<10;

  run([]{
    auto A = gmalloc<int64_t>(SZ);
    //ProfilerStart("ring.prof");
    auto start = time();
    for(int i = 0; i < 1024; ++i){
      pfor(A, SZ, [](int64_t* a){
        *a = 10;
      });
    }
    auto end = time();
    //ProfilerStop();
    //double _time = end-start;
    double _time = diff(start, end)/1000000.0;
    std::cout << "TIME is " <<  _time  <<  " sec." << std::endl;
  });//run
}

void barrier_test(){
  using namespace Charm;

  run([]{
    auto start = time();
    all_do([]{
      for(int i = 0 ; i < 100; ++i){
        barrier();
      }
    });
    auto end = time();
    double _time = diff(start, end)/1000000.0;
    std::cout << "TIME is " <<  _time  <<  " sec." << std::endl;
  });//run
}

int main(int argc, char* argv[]){
  using namespace Charm;
  CHARM_Init(&argc, &argv);
  test11();
  CHARM_Finalize();
  return 0;
}
