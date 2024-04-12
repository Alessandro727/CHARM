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

void GUPS(){
  using namespace Charm;
  const int64_t SZ=1<<30;
  run([]{

    auto A = gmalloc<int64_t>(SZ);
    auto B = gmalloc<int64_t>(SZ);

    pfor(B, SZ, [](int64_t* b){
      *b = random() % (SZ);
    });

    //sync_printf("hello");
    //ProfilerStart("ring.prof");
    //MLOG.on();
    auto start = time();
    pfor(B, SZ, [A](int64_t i, int64_t* b){
      int64_t x = *b;
      auto gp = A+x;
      //async
      call<async>(gp, [i](int64_t* w){
        *w ^= i; 
      });
    });//pfor
    auto end = time();
    //MLOG.off();
    //ProfilerStop();

    double _time = diff(start, end)/1000000.0;
    double gups = SZ/(_time);
    std::cout << "GUPS:" << gups << " in " <<  _time  <<  " sec." << std::endl;
    mlog_dump();
    //MLOG.dump();
  });//run
}

// run with arbitrary node
void GUPS1(){
  using namespace Charm;
  run([]{
    const int SZ=1<<20;
    auto A = (int*)malloc(SZ*sizeof(int));
    auto B = (int*)malloc(SZ*sizeof(int));
    for(int i = 0; i < SZ; ++i){
      B[i] = random() % (SZ);
    }

    auto start = time();
    for(int i = 0; i < SZ; ++i){
      A[B[i]] ^= i;
    }
    auto end = time();

    double _time = diff(start, end)/1000000.0;
    double gups = SZ/(_time);
    std::cout << "GUPS:" << gups << " in " <<  _time  <<  " sec." << std::endl;
  });//run
}


int main(int argc, char* argv[]){
  using namespace Charm;
  CHARM_Init(&argc, &argv);
  GUPS(); // shoule be 5e7
  CHARM_Finalize();
  return 0;
}
