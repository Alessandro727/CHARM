## CHARM: Chiplet Heterogeneity-Aware Runtime Mapping System

CHARM is a C++ runtime system for scaling and improving applications performance on chiplet-based processors.


## Dependences

You must have a 64-bit Linux system with the following installed to build CHARM.

- Build
    - CMake >= 2.8.12
- Compiler
    - GCC > 4.8
- External:
    - libpfm
    - MPI
    - pthread
- optional
    - libnuma
    - gperftool

## Quick Start

Unlike Grappa and Ring, which promote a unified shared memory model, Charm emphasizes coordinated data distribution and task division among processes. It addresses not just NUMA architectures but also the heterogeneity of partitioned L3 caches in chiplet-based processors.

### Hello world

You can use `run()` to capture the whole process you want to run. Use `all_do()` to spawn the same task on all cores just like what you do in SPMD. Use `call()` to perform a remote process call.

`void run([]{ /*your codes*/ });`    
`void all_do([]{ /*your works*/ });`    

`void call(GlobalAddress<T> gt, [](T* t){} );`    
`auto f = call(GlobalAddress<T> gt, [](T* t){} );`   
`T result = f.get() // this will block explicitly just like C++11 future`  

`void call(int which_core, [](){} );`    
`auto f = call(int which_core, [](){} );`  
`T result = f.get() // this will block explicitly just like C++11 future`  


```c++
#include <iostream>
#include <vector>
#include <cstdio>
#include "charm.h"

int x;

int main(int argc, char* argv[]){
  CHARM_Init(&argc, &argv);
  run([]{
    all_do([]{
      x=0;
      if(thread_rank() == 0){
        auto gx = make_global(0, 1, &x);
        call(gx, [](int *x){
           sync_printf("hello world", (*x));
        });//call
      }
    });//all_do
  });//run
  CHARM_Finalize();
  return 0;
}
```
