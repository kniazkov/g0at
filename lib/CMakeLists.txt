# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

# Universal utilities. This target must never depend on core or Goat VM objects.
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/zlib" "${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib")
file(GLOB_RECURSE common_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_library(goat_common STATIC ${common_sources})
set_target_properties(goat_common PROPERTIES
  OUTPUT_NAME common ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(goat_common PUBLIC "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(goat_common PUBLIC "$<$<NOT:$<CONFIG:Release>>:MEMORY_DEBUG>")
target_link_libraries(goat_common PUBLIC goat_minizip)
if(WIN32)
  target_link_libraries(goat_common PUBLIC bcrypt advapi32 shell32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(goat_common PUBLIC Threads::Threads)
endif()
