
//#include "Cyclades_sparse_sgd.cpp"

#include "charm.h"

#include "logistic_regression_sparse_sgd.cpp"
#include "logistic_regression_dense_scd.cpp"
#include "logistic_regression_dense_sgd.cpp"


using namespace Charm;

/**
 * \brief This is one example of running SGD for logistic regression
 * in DimmWitted. You can find more examples in test/glm_dense.cc
 * and test/glm_sparse.cc, and the documented code in 
 * app/glm_dense_sgd.h
 */
int main(int argc, char** argv){
  
  CHARM_Init(&argc, &argv);
  run([]{
//    call<async>(0, []{
        double rs;

        std::cout << "\n GLM SPARSE SGD: PER MACHINE \n" << std::endl;
        std::cout << "DW_DATAREPL_SHARDING \n" << std::endl;
        rs = test_glm_sparse_sgd<DW_MODELREPL_PERMACHINE, DW_DATAREPL_SHARDING>();
//	rs = test_glm_sparse_sgd<DW_MODELREPL_PERMACHINE, DW_DATAREPL_FULL>();
//        rs = test_glm_sparse_sgd<DW_MODELREPL_PERNODE, DW_DATAREPL_SHARDING>();
        std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;

//      });//call
  
  //std::cout << "DW_DATAREPL_FULL \n" << std::endl;
  //rs = test_glm_sparse_sgd<DW_MODELREPL_PERMACHINE, DW_DATAREPL_FULL>();
  //std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;
/*
  std::cout << "\n GLM SPARSE SGD: PER NODE \n" << std::endl;

  std::cout << "DW_DATAREPL_SHARDING \n" << std::endl;
  rs = test_glm_sparse_sgd<DW_MODELREPL_PERNODE, DW_DATAREPL_SHARDING>();
  std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;
*/
/*  std::cout << "DW_DATAREPL_FULL \n" << std::endl;
  rs = test_glm_sparse_sgd<DW_MODELREPL_PERNODE, DW_DATAREPL_FULL>();
  std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;

  std::cout << "\n GLM SPARSE SGD: PER CORE \n" << std::endl;

  std::cout << "DW_DATAREPL_SHARDING \n" << std::endl;
  rs = test_glm_sparse_sgd<DW_MODELREPL_PERCORE, DW_DATAREPL_SHARDING>();
  std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;

  std::cout << "DW_DATAREPL_FULL \n" << std::endl;
  rs = test_glm_sparse_sgd<DW_MODELREPL_PERCORE, DW_DATAREPL_FULL>();
  std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;
*/
  });
  CHARM_Finalize();

  // double rs;
  // std::cout << "\n GLM SPARSE SGD: PER MACHINE \n" << std::endl;
  //       std::cout << "DW_DATAREPL_SHARDING \n" << std::endl;
  //       rs = test_glm_sparse_sgd<DW_MODELREPL_PERMACHINE, DW_DATAREPL_SHARDING>();
  //       std::cout << "SUM OF MODEL: " << rs << "\n" << std::endl;
  return 0;
}
