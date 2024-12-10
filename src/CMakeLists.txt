# Copyright 2025 Ivan Kniazkov

# Use of this source code is governed by an MIT-style license
# that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

cmake_minimum_required(VERSION 3.10)
project(goat)
include_directories(.)

file(GLOB core_sources
  lib/*.c
  codegen/*.c
  model/*.c
  vm/*.c
)
add_library(core STATIC ${core_sources})
message(STATUS "Found core sources: ${core_sources}")

file(GLOB goat_exe main.c)
message(STATUS "Found interpreter sources: ${goat_exe}")
add_executable(goat ${goat_exe})
target_link_libraries (goat core pthread)

file(GLOB unit_testing_exe unit_testing.c test/*.c)
message(STATUS "Found unit testing sources: ${unit_testing_exe}")
add_executable(unit_testing ${unit_testing_exe})
target_link_libraries (unit_testing core pthread)

add_definitions(-DMEMORY_DEBUG)
