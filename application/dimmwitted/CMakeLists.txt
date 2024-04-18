set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -I./lib/libunwind-1.1/include -L./lib/numactl-2.0.9 -I./lib/numactl-2.0.9 -I./src ")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lrt -lnuma -pthread")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src /usr/include)

add_executable(example examples/example.cpp)
target_link_libraries(example Charm)
