# Shared module infrastructure, independent of core and the Goat VM.
add_library(goat_module STATIC identity.c module_json.c)
target_include_directories(goat_module PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(goat_module PUBLIC yyjson)
set_target_properties(goat_module PROPERTIES POSITION_INDEPENDENT_CODE ON)
