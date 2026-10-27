# Shared module infrastructure, independent of core and the Goat VM.
add_library(goat_module STATIC identity.c module_json.c module_information_merge.c json_canonical.c module_normalize.c module_hash.c module_container.c module_native.c module_native_merge.c module_native_host.c module_abi.c module_guards.c)
target_include_directories(goat_module PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(goat_module PUBLIC yyjson goat_common)
set_target_properties(goat_module PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT WIN32)
    target_link_libraries(goat_module PUBLIC m)
endif()
