# CMakeLists.txt for streamcluster

##cmake_minimum_required(VERSION 3.10)

# Set the project name and version
##project(streamcluster_shoal VERSION 1.0 LANGUAGES C CXX)

# Set the C++ standard and compiler options
##set(CMAKE_CXX_STANDARD 11)
##set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set compilation flags
##set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DENABLE_THREADS -pthread")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Define the source files
##set(SRCS
##    streamcluster.cpp
##    parsec_barrier.cpp
##)

# Create the executable
##add_executable(streamcluster_shoal ${SRCS})
##target_link_libraries(streamcluster_shoal Charm)

# Installation setup
##set(INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/pkgs/kernels/streamcluster/inst/${CMAKE_SYSTEM_PROCESSOR}/bin")
##install(TARGETS streamcluster_shoal DESTINATION ${INSTALL_PREFIX})

# Clean target setup
##add_custom_target(clean_all_shoal
##    COMMAND ${CMAKE_COMMAND} -E remove -f *.o streamcluster_shoal
##    COMMENT "Cleaning up generated files"
##)

##----------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)
project(streamcluster CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define variables
set(TARGET streamcluster_shoal)
set(PARSECDIR $ENV{PARSECDIR})
set(PARSECPLAT $ENV{PARSECPLAT})
set(PREFIX ${PARSECDIR}/pkgs/kernels/streamcluster/inst/${PARSECPLAT})

# Set build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Source files
set(SOURCES
    streamcluster.cpp
    parsec_barrier.cpp
    crc.c
)

# Shoal integration
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(BASE ${ROOT}/../libshoal/)
set(SHOAL ${BASE}/shoal/)

# Add Shoal source files
list(APPEND SOURCES
    ${SHOAL}/src/misc.cpp
    ${SHOAL}/src/linux.cpp
    ${SHOAL}/src/shl.cpp
)

# Include directories
include_directories(${SHOAL}/inc)

# Add executable
add_executable(${TARGET} ${SOURCES})

# Compiler flags
target_compile_definitions(${TARGET} PRIVATE ENABLE_THREADS)
target_compile_options(${TARGET} PRIVATE -pthread -fopenmp)

# Get Git version
execute_process(
  COMMAND git describe --abbrev=4 --dirty --always
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
target_compile_definitions(${TARGET} PRIVATE VERSION="${GIT_VERSION}")

# Debug/Release specific flags
#if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#  target_compile_options(${TARGET} PRIVATE -g -O0 -pg -fopenmp)
#  target_compile_definitions(${TARGET} PRIVATE SHL_DEBUG)
#else()
#  target_compile_options(${TARGET} PRIVATE -O3 -fopenmp)
#endif()

# Link libraries
target_link_libraries(${TARGET} PRIVATE
    numa
    papi
    pfm
    ${SHOAL}/libshl.so
    lua5.2
    m
    dl
    Charm
)

target_link_libraries(streamcluster_shoal PRIVATE atomic)

# Custom target for building libshoal
#add_custom_target(libshoal
#    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${SHOAL} BUILDTYPE=${CMAKE_BUILD_TYPE}
#    WORKING_DIRECTORY ${SHOAL}
#)
#add_dependencies(${TARGET} libshoal)

#target_link_libraries(streamcluster_shoal Charm)

# Installation setup
set(INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/pkgs/kernels/streamcluster/inst/${CMAKE_SYSTEM_PROCESSOR}/bin")
install(TARGETS streamcluster_shoal DESTINATION ${INSTALL_PREFIX})

# Clean target setup
add_custom_target(clean_all_shoal
    COMMAND ${CMAKE_COMMAND} -E remove -f *.o streamcluster_shoal
    COMMENT "Cleaning up generated files"
)


