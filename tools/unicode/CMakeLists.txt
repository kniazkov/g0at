# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
cmake_minimum_required(VERSION 3.13)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(goat_unicode LANGUAGES C)
endif()
get_filename_component(GOAT_UNICODE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# No dependency on goat_common or the generated Unicode table. Cross builds
# configure this directory separately using a host compiler; never run target tools.
add_executable(unicode_generator main.c "${GOAT_UNICODE_ROOT}/lib/hash/sha256.c")
target_include_directories(unicode_generator PRIVATE "${GOAT_UNICODE_ROOT}")
target_compile_definitions(unicode_generator PRIVATE GOAT_UNICODE_ROOT="${GOAT_UNICODE_ROOT}")
set_target_properties(unicode_generator PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(CTest)
  add_test(NAME unicode.generator.check COMMAND unicode_generator --check --root "${GOAT_UNICODE_ROOT}")
  set_tests_properties(unicode.generator.check PROPERTIES TIMEOUT 30)
endif()
