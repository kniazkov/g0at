# Copyright 2025 Ivan Kniazkov

# Use of this source code is governed by an MIT-style license
# that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

include(TestBigEndian)
test_big_endian(GOAT_BIG_ENDIAN)
if(GOAT_BIG_ENDIAN)
  message(FATAL_ERROR "Goat v1 supports only little-endian targets")
endif()
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/../include")
check_c_source_compiles("#include <goat/unicode.h>
_Static_assert(U'😀' == 0x1f600, \"UTF-32 required\");
int main(void) { return 0; }" GOAT_UTF32_TARGET)
if(NOT GOAT_UTF32_TARGET)
  message(FATAL_ERROR "Goat requires known little-endian UTF-32 char32_t and 8-bit bytes")
endif()
include_directories(. ../include)

add_library(yyjson STATIC ../third_party/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/yyjson")
set_target_properties(yyjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zlib" "${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib")
add_library(goat_zip STATIC lib/zip_archive.c)
target_include_directories(goat_zip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(goat_zip PRIVATE goat_minizip)
set_target_properties(goat_zip PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory(module)

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
list(REMOVE_ITEM core_sources "${CMAKE_CURRENT_SOURCE_DIR}/lib/sha256.c" "${CMAKE_CURRENT_SOURCE_DIR}/lib/zip_archive.c")
list(REMOVE_ITEM core_sources "${CMAKE_CURRENT_SOURCE_DIR}/common/module_build.c" "${CMAKE_CURRENT_SOURCE_DIR}/common/module_compilation.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_validation.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_writer.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_search.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_dependencies.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_graph.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_graph_diagnostics.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_snapshot.c" "${CMAKE_CURRENT_SOURCE_DIR}/vm/module_loading.c")
# Build the native runtime separately; never depend on VM internals or its allocator.
add_library(goat_native STATIC native/strings.c native/str.c lib/number_format.c lib/unicode.c)
set_target_properties(goat_native PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  OUTPUT_NAME goat_native PREFIX "lib" SUFFIX ".a"
  POSITION_INDEPENDENT_CODE ON INTERPROCEDURAL_OPTIMIZATION FALSE
  C_VISIBILITY_PRESET hidden)
target_include_directories(goat_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
# Bundle the matching headers and archive without modifying the source tree.
set(native_headers_dir "${CMAKE_CURRENT_SOURCE_DIR}/../include/goat")
set(native_runtime_data "${CMAKE_BINARY_DIR}/generated/native_runtime_data.inc")
add_custom_command(
  OUTPUT "${native_runtime_data}"
  COMMAND "${CMAKE_COMMAND}"
    "-DINPUT_DIR=${native_headers_dir}"
    "-DARCHIVE_FILE=$<TARGET_FILE:goat_native>"
    "-DOUTPUT_FILE=${native_runtime_data}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/embed_native_runtime.cmake"
  DEPENDS
    goat_native
    "${native_headers_dir}/unicode.h"
    "${native_headers_dir}/native_abi.h"
    "${native_headers_dir}/native_runtime.h"
    "${native_headers_dir}/native_strings.h"
    "${native_headers_dir}/module_abi.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/strings.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/str.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/number_format.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/number_format.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/unicode.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/unicode_data.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/embed_native_runtime.cmake"
  COMMENT "Embedding native runtime headers and static library"
  VERBATIM
)
add_library(core STATIC ${core_sources} "${native_runtime_data}")
set_target_properties(core PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(core PUBLIC goat_module goat_zip)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(core PUBLIC Threads::Threads)
endif()
target_include_directories(core PRIVATE "${CMAKE_BINARY_DIR}/generated")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
  OUTPUT_VARIABLE goat_build_id OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT goat_build_id MATCHES "^[0-9a-f]+$")
  set(goat_build_id "source-v1")
endif()
target_compile_definitions(core PRIVATE GOAT_BUILD_ID="${goat_build_id}")

if(CMAKE_DL_LIBS)
  target_link_libraries(core PUBLIC ${CMAKE_DL_LIBS})
endif()
if(WIN32)
  target_link_libraries(core PUBLIC bcrypt advapi32 shell32)
endif()
# Keep the allocator API consistent between core and its consumers.
target_compile_definitions(core PUBLIC "$<$<NOT:$<CONFIG:Release>>:MEMORY_DEBUG>")
message(STATUS "Found core sources: ${core_sources}")

file(GLOB goat_exe main.c)
message(STATUS "Found interpreter sources: ${goat_exe}")
add_executable(goat-merge merge.c)
target_link_libraries(goat-merge core goat_validation goat_module m)
add_executable(goat-inspect inspect.c)
target_link_libraries(goat-inspect core goat_validation goat_module m)
add_executable(goat ${goat_exe})
target_link_libraries (goat core goat_validation goat_module m)

# One validation library for compiler, loader, inspect and merge.
add_library(goat_validation STATIC common/module_build.c common/module_compilation.c vm/module_validation.c vm/module_writer.c vm/module_search.c vm/module_dependencies.c vm/module_graph.c vm/module_graph_diagnostics.c vm/module_snapshot.c vm/module_loading.c)
target_link_libraries(goat_validation PUBLIC core)
target_include_directories(goat_validation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
