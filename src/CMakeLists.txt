# Copyright 2025 Ivan Kniazkov

# Use of this source code is governed by an MIT-style license
# that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

cmake_minimum_required(VERSION 3.10)
project(goat LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include_directories(.)

file(GLOB core_sources
  lib/*.c
  resources/*.c
  common/*.c
  scanner/*.c
  parser/*.c
  graph/*.c
  analysis/*.c
  builtins/*.c
  codegen/*.c
  model/*.c
  vm/*.c
  cli/*.c
)
add_library(core STATIC ${core_sources})
if(WIN32)
  target_link_libraries(core PUBLIC bcrypt)
endif()
# Keep the allocator API consistent between core and its consumers.
target_compile_definitions(core PUBLIC "$<$<NOT:$<CONFIG:Release>>:MEMORY_DEBUG>")
message(STATUS "Found core sources: ${core_sources}")

file(GLOB goat_exe main.c)
message(STATUS "Found interpreter sources: ${goat_exe}")
add_executable(goat ${goat_exe})
target_link_libraries (goat core pthread m)

file(GLOB unit_testing_exe unit_testing.c test/*.c)
message(STATUS "Found unit testing sources: ${unit_testing_exe}")
add_executable(unit_testing ${unit_testing_exe})
target_link_libraries (unit_testing core pthread m)

add_executable(analysis_testing analysis_testing.c)
target_link_libraries(analysis_testing core pthread m)

add_executable(functional_testing functional_testing.c)
