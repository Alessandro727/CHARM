# CMakeLists.txt for streamcluster

cmake_minimum_required(VERSION 3.10)

# Set the project name and version
project(streamcluster VERSION 1.0 LANGUAGES C CXX)

# Set the C++ standard and compiler options
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DENABLE_THREADS -pthread")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Define the source files
set(SRCS
    streamcluster.cpp
    parsec_barrier.cpp
)

# Create the executable
add_executable(streamcluster ${SRCS})
target_link_libraries(streamcluster Charm)

# Installation setup
set(INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/pkgs/kernels/streamcluster/inst/${CMAKE_SYSTEM_PROCESSOR}/bin")
install(TARGETS streamcluster DESTINATION ${INSTALL_PREFIX})

# Clean target setup
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o streamcluster
    COMMENT "Cleaning up generated files"
)
