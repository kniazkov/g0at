# Copyright 2023 Ivan Kniazkov

# Use of this source code is governed by an MIT-style license
# that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

project(goat)
cmake_minimum_required(VERSION 3.6)
include_directories(.)

file(GLOB core_sources
  lib/*.cpp
  resources/*.cpp
  model/*.cpp
  compiler/scanner/*.cpp
)
add_library(core STATIC ${core_sources})

file(GLOB goat_exe main.cpp)
add_executable(goat ${goat_exe})
target_link_libraries (goat core pthread)

file(GLOB unit_testing_exe unit_testing.cpp test/*.cpp)
add_executable(unit_testing ${unit_testing_exe})
target_link_libraries (unit_testing core pthread)
