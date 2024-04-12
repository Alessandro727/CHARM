/* edgelist.h

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

#ifndef _EDGELIST_H_
#define _EDGELIST_H_

// high-level Graph Abstract
#include <string>
#include <iostream>
#include <fstream>

#include "graph_generator.h"
#include "utils/IO.h"
#include "charm.h"

namespace Charm{


template<typename T>
struct Edge{
  uint32_t v0,v1;
  T data;
  static_assert(sizeof(T) == 8, "Edge size should equeal to 8");

  bool input(std::ifstream& infile){
    infile >> v0;
    if(!infile.good()) return false;
    infile >> v1;
    if(!infile.good()) return false;
    infile >> data;
    return true;
  }
};

template<>
struct Edge<Empty>{
  uint32_t v0, v1;
  bool input(std::ifstream& infile){
    infile >> v0;
    if(!infile.good()) return false;
    infile >> v1;
    return true;
  }
};


class EdgeList {
private:
  bool initialized;

  static EdgeList load_tsv(std::string path, bool is_weight);
  //static EdgeList load_bin( std::string path);
  void save_bin(std::string path, bool is_weight);
	void save_mtx(std::string path, bool is_weight);
  
public:
  GlobalAddress<Edge<double>> edges;
  int64_t nedge; 
  int64_t nv;

  
  static EdgeList Kronecker(int scale, int64_t nedge, uint64_t seed1, 
                            uint64_t seed2);

  static EdgeList Load( std::string path, std::string format, bool is_weight=false){
    if( format == "tsv" || format == "TSV"){
      return load_tsv(path, is_weight);
    }else{
      ASSERT(false, "not implement");
      return EdgeList();
    }
  }

  inline void random_weight(){
    pfor(edges, nedge, [](Edge<double>* e) {  
      e->data = drand48();
    });
  }

  inline void destroy() { gfree(edges); }

  // default contstructor
  EdgeList()
    : initialized( false )
    , edges()
    , nedge(0)
  {}

  EdgeList(const EdgeList& el): initialized(false), edges(el.edges), nedge(el.nedge) { }

  EdgeList& operator=(const EdgeList& el) {
    if( initialized ) {
      gfree(edges);
    }
    edges = el.edges;
    nedge = el.nedge;
    return *this;
  }

  void save( std::string path, std::string format, bool is_weight ){
    if( format == "binedgelist" ){
      return save_bin(path, is_weight);
    }else if( format == "mtx" ){
			return save_mtx(path, is_weight);
		}else{
      ASSERT(false, "not implement");
    }
  }

protected:
  EdgeList(int64_t nedge): initialized(true), nedge(nedge) {}
  EdgeList(int64_t nedge, int64_t nv): initialized(true), nedge(nedge), nv(nv) {}
  
};



};//namespace Charm

#endif
