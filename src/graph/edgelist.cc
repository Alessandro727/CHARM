/* edgelist.cc

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

#include "graph/edgelist.h"
#include "graph_generator.h"
#include "make_graph.h"

namespace Charm{

thread_local std::vector<Edge<Empty>> read_uw_edges;
thread_local std::vector<Edge<double>> read_w_edges;
thread_local size_t local_offset;


EdgeList EdgeList::load_tsv( std::string path, bool is_weight=false){
  double start = walltime();
  // make sure file exists/
  ASSERT(file_exists(path), "File not exist");

  size_t fsize = file_size( path );
  size_t path_length = path.size() + 1;

  ASSERT(path_length < 256, "file path is too long")

  char filename[256];
  strncpy( &filename[0], path.c_str(), 256 );

  auto bytes_per_core = fsize / cores();

  // read into temporary buffer
  all_do( [=] {
    auto my_bytes_per_core = bytes_per_core;
    local_offset = my_id();
    int64_t start_offset = bytes_per_core*local_offset;
    if( my_id()+1 == (size_t)cores()){
      my_bytes_per_core += fsize - (bytes_per_core * cores());
    }
    int64_t end_offset = start_offset + my_bytes_per_core;

    // start reading at start offset
    std::ifstream infile( filename, std::ios_base::in );
    infile.seekg(start_offset);

    if( start_offset > 0 ) {
      std::string s;
      std::getline( infile, s );
    }

    start_offset = infile.tellg();

    // read up to one entry past the end_offset
    while( infile.good() && start_offset < end_offset ) {
      if( infile.peek() == '#' ) { // if a comment
        std::string str;
        std::getline( infile, str );
      } else {
        if(is_weight) {
          Edge<double> tmp_e;
          if(!tmp_e.input(infile)) break;
          read_w_edges.push_back(tmp_e);
        }else{
          Edge<Empty> tmp_e;
          if(!tmp_e.input(infile)) break;
          read_uw_edges.push_back(tmp_e);
        }
        start_offset = infile.tellg();
      }
    }
    // collect sizes
    if(is_weight) local_offset = read_w_edges.size();
    else local_offset = read_uw_edges.size();
  });

  auto nedge = reduce(local_offset, collective_add);
  std::cout << " read data over, now convert to structured data." << std::endl;

  EdgeList el( nedge );
  el.edges = gmalloc<Edge<double>>(nedge);
  auto edges = el.edges;

  all_do( [=] {
    auto rge = edges.get_my_local_data(my_id(), nedge);
    auto* local_ptr = reinterpret_cast<Edge<double>*>(rge.paddr);
    size_t local_count = rge.n;
    size_t read_count;
    if(is_weight) read_count = read_w_edges.size();
    else read_count = read_uw_edges.size();

    // copy everything in our read buffer that fits locally
    auto local_max = std::min( local_count, read_count );
    if(is_weight){
      std::memcpy( local_ptr, &read_w_edges[0], local_max * sizeof(Edge<double>) );
    }else{
      for(size_t i = 0; i < local_max; ++i){
        local_ptr[i].v0 = read_uw_edges[i].v0;
        local_ptr[i].v1 = read_uw_edges[i].v1;
        local_ptr[i].data = 0;
      }
    }

    local_offset = local_max;
    barrier();

    // get rid of remaining edges
    auto mycore = my_id();
    auto likely_consumer = (mycore + 1) % cores();
    while( local_max < read_count ) {
      Edge<double> e;
      if(is_weight){
        e = read_w_edges[local_max];
      }else{
        e.v0 = read_uw_edges[local_max].v0;
        e.v1 = read_uw_edges[local_max].v1;
        e.data = 0;
      }

      int retval = call( likely_consumer, [=]{
        auto rge = edges.get_my_local_data(my_id(), nedge);
        Edge<double> * local_ptr = reinterpret_cast<Edge<double>*>(rge.paddr);
        auto local_count = rge.n;

        // do we have space to insert here?
        if (local_offset < local_count) {
          // yes, so do so
          local_ptr[local_offset] = e;
          local_offset++;
          if (local_offset < local_count) {
            return 0; // succeeded with more space available
          } else {
            return 1; // succeeded with no more space available
          }
        } else {
          // no, so return nack.
          return -1; // did not succeed
        }
      });

      // insert succeeded, so move to next edge
      if( retval >= 0 ) {
        local_max++;
      }

      // no more space available on target, so move to next core
      if( local_max < read_count && retval != 0 ) {
        likely_consumer = (likely_consumer + 1) % cores();
        ASSERT( likely_consumer != my_id(), "No more space to place edge on cluster?");
      }
    }

    // wait for everybody else to fill in our remaining spaces
    barrier();

    // discard temporary read buffer
    if(is_weight) read_w_edges.clear();
    else read_uw_edges.clear();
  });

  std::cout << " Load TSV time : "  << walltime() - start << " s." << std::endl;
  // done!
  return el;
}

EdgeList EdgeList::Kronecker(int scale, int64_t nedge, uint64_t seed1,
                        uint64_t seed2){
  double start = walltime();
  EdgeList el(nedge, 1ll<<scale);
  GlobalAddress<packed_edge> tmp;
  make_graph(scale, nedge, seed1, seed2, &el.nedge, &tmp);
  el.edges = GlobalAddress<Edge<double>>(tmp);
  std::cout << " Generate graph time : " << walltime() - start << " s." << std::endl;
  return el;
}

void EdgeList::save_bin(std::string path, bool is_weight){
  ASSERT(false,"not implement");
}

void EdgeList::save_mtx(std::string path, bool is_weight){
  ASSERT(THREAD_SIZE==1, "thread size must equal to 1.");
  std::fstream fo(path, fo.out);
  fo << nv << " " << nedge << std::endl;
  auto iter = iterate_local(edges, nedge);
  for(auto& e : iter){
    fo << e.v0 << " " << e.v1 << std::endl;
  }
}

}; //namespace Charm

