/* sssp.h

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

#ifndef _SSSP_H_
#define _SSSP_H_


#include "charm.h"
#include "graph/graph.h"

using namespace Charm;
struct SSSPData {
  double dist;
  //double new_dist;
  int64_t parent;
  void init() {
    //new_dist = std::numeric_limits<double>::max();
    dist = std::numeric_limits<double>::max();
    parent = -1;
  }
};
struct SSSPEdgeData {
  double data;
};
struct msg{
  double dist;
  int64_t who;
};
using V = Vertex<SSSPData,SSSPEdgeData>;
using G = Graph<SSSPData,SSSPEdgeData>;
double do_sssp(GlobalAddress<G> &g, int64_t root) {
  double start = walltime();
  each_vertex(g, [](V* v){ (*v)->init(); });
  vertex_do(g, root, [=](V* v) { 
      (*v)->dist = 0.0;
      //(*v)->new_dist = 0.0;
      (*v)->parent = root; 
      v->set_imm_active();
  });
  /*---------- CC ----------*/
  auto emit = [g](V* sv){ return msg{(*sv)->dist, g->id(*sv) }; };
  auto edgework = [](msg vmsg, G::Edge* e){ vmsg.dist += (*e)->data;return vmsg; };
  auto slot = [](V* ev, msg emsg){
    if (emsg.dist < (*ev)->dist ){
      (*ev)->dist = emsg.dist;
      (*ev)->parent = emsg.who;
      return true;
    }
    return false;
  };
  auto cond = [](V* v){ return true; };
  /*------------------------*/
  int64_t active_size = 1;
  while (active_size != 0) {
    active_size = graph_do<Sparse>(g, active_size, emit, edgework, slot, cond);
  }//while
  return walltime() - start;
}

#endif
