/* verify.h

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

#ifndef _SSSP_VERIFY_H_
#define _SSSP_VERIFY_H_

#include "charm.h"
#include "graph/graph.h"
#include "sssp.h"

inline int64_t get_parent(GlobalAddress<G> g, int64_t j){
  return call(g->vs+j, [](V* v){ return (*v)->parent; });
}

inline double get_dist(GlobalAddress<G> g, int64_t j) {
  return call(g->vs+j, [](V* v){ return (*v)->dist; });
}

inline double get_min_weight(GlobalAddress<G> g, int64_t s, int64_t e){
  return call(g->vs+s, [=](V* v){
      double ret = std::numeric_limits<double>::max();
      for(int i = 0; i < v->nadj; i++){
        if(v->get_adj()[i] == e) {
          ret = std::min(ret, v->local_edge_state[i].data);
        }
      }
      return ret;
  });
}

inline double verify_sssp(EdgeList& el, GlobalAddress<G> g, int64_t root, bool directed = false) {
  
  double start = walltime();
  // SSSP distances verification
  pfor(el.edges, el.nedge, [=](WEdge* e){
    auto i = e->v0, j = e->v1;

    /* Eliminate self loops from verification */
    if ( i == j ) return;

    /* SSSP specific checks */
    auto ti = get_parent(g,i);
    auto tj = get_parent(g,j);
    auto di = get_dist(g,i), dj = get_dist(g,j);
    auto wij = e->data;

    /* maybe duplicate edge between two vertex */
    auto min_wij = get_min_weight(g, i, j);

    ASSERT( min_wij <= wij, "shorter path exists");
    ASSERT(!((di < dj) && ((di + wij) < dj)), "Error, distance of the nearest neighbor is too great");
    if(!directed) ASSERT(!((dj < di) && ((dj + wij) < di)), "Error, distance of the nearest neighbor is too great");
    ASSERT(!((i == tj) && ((di + min_wij) != dj)), "Error, distance of the child vertex is not equal to");
    if(!directed) ASSERT(!((j == ti) && ((dj + min_wij) != di)), "Error, distance of the child vertex is not equal to");

  });

  // everything checked out!
  return walltime() - start;
}




#endif
