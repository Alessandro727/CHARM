# Install script for directory: /home/fogli/CHARM/application/shoal-base/streamcluster

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal"
         RPATH "")
  endif()
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin" TYPE EXECUTABLE FILES "/home/fogli/CHARM/application/shoal-base/streamcluster/streamcluster_shoal")
  if(EXISTS "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal")
    file(RPATH_CHANGE
         FILE "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal"
         OLD_RPATH "/home/fogli/CHARM/application/shoal-base/streamcluster/../libshoal/shoal:/home/fogli/CHARM/src:/usr/local/lib:"
         NEW_RPATH "")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}/usr/local/pkgs/kernels/streamcluster/inst/x86_64/bin/streamcluster_shoal")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/home/fogli/CHARM/application/shoal-base/streamcluster/CMakeFiles/streamcluster_shoal.dir/install-cxx-module-bmi-Release.cmake" OPTIONAL)
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/fogli/CHARM/application/shoal-base/streamcluster/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
