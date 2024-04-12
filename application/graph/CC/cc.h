/* cc.h

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

#ifndef _CC_H_
#define _CC_H_

#include "charm.h"

#include "graph/graph.h"

using namespace Charm;

struct CCData{
  int64_t color;
  void init(int64_t id){
    color = id;   
  }
};
using V = Vertex<CCData, Empty>;
using G = Graph<CCData, Empty>;
double do_cc(GlobalAddress<G> &g){
  double start = walltime();
  each_vertex(g,[=](V* v){ 
    (*v)->init( g->id(*v) ); 
    v->set_imm_active(); 
  });
  /*---------- CC ----------*/
  auto emit = [](V* sv) { return (*sv)->color; };
  auto edgework = [](int64_t vdata, G::Edge* e) {return vdata;};
  auto slot = [](V* ev, int64_t color){
    if(color < (*ev)->color){
      (*ev)->color = color;
      return true;
    }
    return false;
  };
  auto cond = [](V* v){ return true; };
  /*------------------------*/
  int64_t active_size = 1;
  while( active_size !=0 ){
    // sparse is faster than all
    active_size = graph_do<Sparse>(g, active_size, emit, edgework, slot, cond);
  }
  
  return walltime() - start;
}

#endif
