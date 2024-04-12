/* pagerank.h

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

#ifndef _PAGERANK_H_
#define _PAGERANK_H_

#include <cmath>

#include "charm.h"
#include "graph/graph.h"

using namespace Charm;
const float D = 0.85; // damping factor
const float eps = 1e-9;
struct PageRankData{
  float pr;
  float old_pr;
  float delta;
  void init(int64_t nv){
    old_pr = 0;
    pr = (1-D)/nv; 
    delta = 0;
  }
  bool update(int64_t outd){
    if ( fabs(delta) > eps) {
      old_pr = pr;
      pr += delta*D/outd;
      delta = 0;
      return true;
    }
    return false;
  }
};
using V = Vertex<PageRankData, Empty>;
using G = Graph<PageRankData, Empty>;
double do_pagerank(GlobalAddress<G> &g){
  double start = walltime();
  each_vertex(g, [=](V* v){ 
    (*v)->init(g->nv); 
    v->set_imm_active();
  });
  /*---------- PR ----------*/
  auto emit = [](V* sv){ return ((*sv)->pr - (*sv)->old_pr)/sv->nadj; };
  auto edgework = [](float vdata, G::Edge* e) { return vdata; };
  auto slot = [](V* ev, float edata){
    (*ev)->delta += edata;
    return false;
  };
  auto cond = [](V* v){ return true; };
  /*------------------------*/
  int64_t active_size = 1;
  while(active_size !=0){
    // scatter
    graph_do<Sparse>(g, active_size, emit, edgework, slot, cond);
    // update
    each_vertex(g, [=](VertexID vid, V* v){
      if ((*v)->update(v->nadj))
        v->set_active();
    });
    active_size = count_active_size(g);
  }
  return walltime() - start;
}

#endif

