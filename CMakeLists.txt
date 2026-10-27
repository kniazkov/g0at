# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

cmake_minimum_required(VERSION 3.13)
project(goat LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Keep the executable paths consumed by scripts and packages stable while the
# source directories are split into separate build descriptions.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Separate fault-injection builds only. Production binaries expose no failure knob.
option(GOAT_TEST_ALLOC_FAILURE "Instrument allocations for loader resource tests" OFF)
if(GOAT_TEST_ALLOC_FAILURE)
  add_library(goat_allocation_fault STATIC "${CMAKE_CURRENT_SOURCE_DIR}/test/src/component/allocation_fault.c")
  link_libraries(goat_allocation_fault)
  add_compile_options("-include" "${CMAKE_CURRENT_SOURCE_DIR}/test/src/component/allocation_fault.h")
  add_compile_definitions(GOAT_TEST_ALLOC_FAILURE)
endif()

add_subdirectory(lib)
add_subdirectory(src)
include(CTest)
add_subdirectory(tools)
add_subdirectory(test)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/package.cmake")
