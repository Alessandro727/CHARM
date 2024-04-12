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

#include "bfs.h"
#include "verify.h"

#include "charm.h"
#include "graph/graph.h"

using namespace Charm;

const int scale = 24;
const int edgefactor = 16;

template< typename T, typename E >
inline int64_t choose_root(GlobalAddress<Graph<T,E>> g) {
  int64_t root;
  do {
    root = random() % g->nv;
  } while (call(g->vs+root,[](Vertex<T,E>* v){ return v->nadj; }) == 0);
  return root;
}


void bfs(EdgeList& tg, GlobalAddress<G> g){

  GlobalAddress<int64_t> bfs_tree = gmalloc<int64_t>(g->nv);
  
  int64_t bfs_nedge;
  bool verify = false;
  //bool verify = true;
  int64_t root = choose_root(g);

  Charm::memset(bfs_tree, -1, g->nv);

  double bfs_time = make_bfs_tree(g, bfs_tree, root);
  std::cout << "# bfs time : " << bfs_time << " s";

  if(verify){
    bfs_nedge = verify_bfs_tree(bfs_tree, g->nv-1, root, &tg);
    if(bfs_nedge < 0){
      std::cout << "   failed verification! " <<  bfs_nedge << std::endl;
    }else{
      std::cout << " passed!" << std::endl;
    }
  }else{
    bfs_nedge = g->nadj >> 1;
    std::cout << "   skip verfication... "<< std::endl;
  }
  gfree(bfs_tree);
  std::cout << "# TEPS : " << bfs_nedge/bfs_time << std::endl;
}


int main(int argc, char* argv[]){
  CHARM_Init(&argc, &argv);
  run([]{
    int64_t nvtx_scale = ((int64_t)1L) << scale;
    int64_t desired_nedge = nvtx_scale * edgefactor;
    EdgeList el;
    el = EdgeList::Kronecker(scale, desired_nedge, 111, 222);
    //el = EdgeList::Load("/mnt/lustre/mengke/twitter/out.twitter", "tsv");
    //el = EdgeList::Load("/mnt/lustre/mengke/wiki-en-cat/out.wiki-en-cat", "tsv");

    auto g = G::Undirected(el, false);
    bfs(el, g);

    mlog_dump();
  });
  CHARM_Finalize();
  return 0;
}
