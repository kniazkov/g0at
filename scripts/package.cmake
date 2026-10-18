# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

# MinGW runtime DLLs must not be required on the destination computer.
if(MINGW)
  target_link_libraries(goat "$<$<CONFIG:Release>:-static>" "$<$<CONFIG:Release>:-static-libgcc>")
endif()

set(package_root "${CMAKE_CURRENT_LIST_DIR}/..")
install(TARGETS goat RUNTIME DESTINATION bin CONFIGURATIONS Release)
install(TARGETS goat_native ARCHIVE DESTINATION lib CONFIGURATIONS Release)
install(FILES "${native_headers_dir}/native_abi.h" "${native_headers_dir}/native_runtime.h"
  "${native_headers_dir}/native_strings.h"
  DESTINATION include/goat CONFIGURATIONS Release)
install(FILES "${package_root}/LICENSE.txt" "${package_root}/docs/package/README.md"
  DESTINATION . CONFIGURATIONS Release)
install(FILES "${package_root}/docs/native-runtime.md" "${package_root}/docs/native-memory.md"
  "${package_root}/docs/native-strings.md"
  DESTINATION docs CONFIGURATIONS Release)
install(FILES "${package_root}/example/hello_world.goat"
  "${package_root}/example/native_string_identity.goat" "${package_root}/docs/package/numeric.goat"
  DESTINATION examples CONFIGURATIONS Release)

execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${package_root}"
  OUTPUT_VARIABLE package_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT package_revision)
  set(package_revision "source")
endif()
math(EXPR package_bits "${CMAKE_SIZEOF_VOID_P} * 8")
set(package_platform "${CMAKE_SYSTEM_NAME}-${package_bits}bit-${CMAKE_C_COMPILER_ID}")
if(DEFINED ENV{MSYSTEM})
  string(APPEND package_platform "-$ENV{MSYSTEM}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt"
  "Revision: ${package_revision}\nPlatform: ${package_platform}\nCompiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\nConfiguration: Release\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build-info.txt" DESTINATION . CONFIGURATIONS Release)
set(CPACK_PACKAGE_NAME goat)
set(CPACK_PACKAGE_VERSION "0.0.0")
set(CPACK_PACKAGE_FILE_NAME "goat-${package_revision}-${package_platform}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
