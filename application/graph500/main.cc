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

#include "generator/make_graph.h"
#include "bfs_local_adj.h"
#include "prng.h"
#include "common.h"
#include "graph.h"
#include "verify.h"

#ifdef PROFILING
#include <google/profiler.h>
#endif

#include "charm.h"

using namespace Charm;

//const int scale = 18;
//const int edgefactor = 32;

const int scale = 24;
const int edgefactor = 16;
const int NBFS_max = 32;
const int BFS_num = 8;

double make_bfs_tree(GlobalAddress<Graph<VertexP>> g_in, GlobalAddress<int64_t> _bfs_tree, int64_t root);
int64_t verify_bfs_tree(GlobalAddress<int64_t> bfs_tree, int64_t max_bfsvtx, int64_t root, tuple_graph * tg);

void output_statis(double bfs_time[], int64_t bfs_nedge[]){
  double avg_time = 0;
  double TEPS = 0;
  for(int i = 1; i < BFS_num; ++i){
    avg_time += bfs_time[i];
  }
  avg_time /= BFS_num-1;

  for(int i = 1; i < BFS_num; ++i){
    TEPS += bfs_nedge[i] / bfs_time[i];
  }
  TEPS /= BFS_num-1;
  std::cout << "Final Result:" << std::endl;
  std::cout << "average time is : " << avg_time << " s." << std::endl;
  std::cout << "average TEPS is : " << TEPS << "." << std::endl;
}

template< typename V >
static void choose_bfs_roots(GlobalAddress<Graph<V>> g, int* nbfs, int64_t bfs_roots[]){
  auto has_adj = [g](int64_t i){
    return call(g->vs+i, [](V* v){
      return v->nadj > 0;
    });
  };
  
  uint64_t counter = 0;
  for(int root_idx = 0; root_idx < *nbfs; ++root_idx){
    int64_t root;
    while(1){
      double d[2];
      make_random_numbers(2, 2, 3, counter, d);
      root = (int64_t)((d[0] + d[1]) * g->nv) % g->nv;
      counter += 2;
      if(counter > 2 * g->nv) break;
      if( !has_adj(root) ) continue;

      int is_duplicate = 0;
      for(int i = 0; i < root_idx; ++i){
        if(root == bfs_roots[i]){
          is_duplicate = 1;
          break;
        }
      }
      if(is_duplicate) continue;
      else break;
    }
    bfs_roots[root_idx] = root;
    std::cout << "root is:" << root << std::endl;
  }

}


void bfs_benchmark(tuple_graph& tg, GlobalAddress<Graph<>> generic_graph, int nroots){
  auto g = Graph<>::transform_vertices<VertexP>(generic_graph, [](VertexP* v){ v->parent(-1); });

  GlobalAddress<int64_t> bfs_tree = gmalloc<int64_t>(g->nv);
  sync_printf(g->nadj/2);
  
  int64_t bfs_roots[NBFS_max];
  double bfs_time[NBFS_max];
  int64_t bfs_nedge[NBFS_max];
  bool verify = true;
  choose_bfs_roots(g, &nroots, bfs_roots);
  
  for(int64_t i=0; i < nroots; ++i){

    //if(i!=0)
      verify = false;

    Charm::memset(bfs_tree, -1, g->nv);

#ifdef PROFILING
    if(i==0) ProfilerStart("graph500.prof");
#endif

    bfs_time[i] = make_bfs_tree(g, bfs_tree, bfs_roots[i]);

#ifdef PROFILING
    if(i==0) ProfilerStop();
#endif

    std::cout << "# " << i << "-th : " << bfs_time[i] << " ";
    if(verify){
      bfs_nedge[i] = verify_bfs_tree(bfs_tree, g->nv-1, bfs_roots[i], &tg);
      if(bfs_nedge[i] < 0){
        std::cout << "failed verification! " <<  bfs_nedge[i] << std::endl;
      }else{
        std::cout << "passed!" << std::endl;
      }
    }else{
      bfs_nedge[i] = g->nadj/2;
      std::cout << "skip verfication... "<< std::endl;
    }
  }
  gfree(bfs_tree);

  output_statis(bfs_time, bfs_nedge);
}


void graph500(){
  int64_t nvtx_scale = ((int64_t)1L) << scale;
  int64_t desired_nedge = nvtx_scale * edgefactor;

  tuple_graph tg;
  //tg.edges = gmalloc<packed_edge>(desired_nedge);

  auto start = time();
  make_graph( scale, desired_nedge, userseed, userseed, &tg.nedge, &tg.edges );
  auto end = time();
  double _time = diff(start, end)/1000000.0;
  std::cout << "prepare graph data : scale=" << scale << " nedge=" << tg.nedge << " in " <<  _time  <<  " sec." << std::endl;

  start = time();
  auto g = Graph<>::create(tg);
  end = time(); 
  _time = diff(start, end)/1000000.0;
  std::cout << "create graph : " <<  _time  <<  " sec." << std::endl;

  bfs_benchmark(tg, g, BFS_num);
}


int main(int argc, char* argv[]){
  CHARM_Init(&argc, &argv);
  run([]{
    graph500();
    mlog_dump();
  });
  CHARM_Finalize();
  return 0;
}
