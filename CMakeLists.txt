# Copyright (c) 2024 Pyarelal Knowles, MIT License

cmake_minimum_required(VERSION 3.20)

project(decodeless_mappedfile)

set(CMAKE_CXX_STANDARD 20)

add_library(decodeless_mappedfile INTERFACE)
target_include_directories(decodeless_mappedfile INTERFACE include)

# cmake namespace style alias
add_library(decodeless::mappedfile ALIAS decodeless_mappedfile)

if(BUILD_TESTING)
  option(BUILD_DECODELESS_TESTING "Enable decodeless testing" ON)
  if(BUILD_DECODELESS_TESTING)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()
