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

#include "pagerank.h"

#include "charm.h"
#include "graph/graph.h"

using namespace Charm;

const int scale = 16;
const int edgefactor = 16;

void pagerank(EdgeList& el, GlobalAddress<G> g){
  //bool verify = true;

  double pagerank_time = do_pagerank(g);

  std::cout << "# pagerank time : " << pagerank_time << " s.";

  std::cout << "   skip verfication... "<< std::endl;
}


int main(int argc, char* argv[]){

  CHARM_Init(&argc, &argv);
  PerfCounter e;
  e.startCounters();

  run([&e] {
    int64_t nvtx_scale = ((int64_t)1L) << scale;
    int64_t desired_nedge = nvtx_scale * edgefactor;
    EdgeList el;
    el = EdgeList::Kronecker(scale, desired_nedge, 111, 222);
    //tg = EdgeList::Load("/mnt/lustre/mengke/twitter/out.twitter", "tsv");

    auto g = G::Undirected(el, false);
    pagerank(el, g);

    e.stopCounters();
    e.printReport(std::cout, 1); // use n as scale factor
    std::cout << std::endl;
    mlog_dump();
  });
  CHARM_Finalize();
  return 0;
}
