# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
cmake_minimum_required(VERSION 3.13)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(goat_module_schema LANGUAGES C)
endif()
get_filename_component(GOAT_SCHEMA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# Deliberately independent of goat/core, lib, and all generated headers. For a
# cross build, configure this directory separately with the host C compiler.
add_executable(module_schema_generator
  main.c "${GOAT_SCHEMA_ROOT}/third_party/yyjson/yyjson.c")
target_include_directories(module_schema_generator PRIVATE "${GOAT_SCHEMA_ROOT}/third_party/yyjson")
target_compile_definitions(module_schema_generator PRIVATE GOAT_SCHEMA_ROOT="${GOAT_SCHEMA_ROOT}")
set_target_properties(module_schema_generator PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
  add_test(NAME format.schema COMMAND module_schema_generator --check --root "${GOAT_SCHEMA_ROOT}")
  set_tests_properties(format.schema PROPERTIES TIMEOUT 30)
endif()
