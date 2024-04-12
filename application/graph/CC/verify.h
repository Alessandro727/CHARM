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

#ifndef _CC_VERIFY_H_
#define _CC_VERIFY_H_

#include "charm.h"
#include "graph/graph.h"
#include "cc.h"

inline int64_t get_color(GlobalAddress<G> g, int64_t i){
  return call(g->vs+i, [](V* v){ return (*v)->color; });
}

//TODO: not strong enough
inline void verify_cc(EdgeList& el, GlobalAddress<G> g){
  pfor(el.edges, el.nedge, [=](WEdge* e){
    auto i = e->v0, j = e->v1;

    /* Eliminate self loops from verification */
    if(i == j) return ;
    
    auto ci = get_color(g,i), cj = get_color(g,j);

    ASSERT( ci == cj, "neighbor color is not same.");
  });
}

#endif
