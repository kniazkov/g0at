# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")

# OS observer is independent of all production process and cleanup implementations.
add_library(goat_process_watch_support STATIC support/process_watch.c)
add_executable(process_watch_testing src/support/process_watch_testing.c)
target_link_libraries(process_watch_testing PRIVATE goat_process_watch_support)

# Shared Python-to-C execution/report helpers remain test-only.
add_library(goat_language_execution_support STATIC support/language_execution.c)
target_link_libraries(goat_language_execution_support PUBLIC goat_test_support)
add_executable(loop_index_testing src/functional/loop_index_testing.c)
target_link_libraries(loop_index_testing PRIVATE goat_language_execution_support)
add_dependencies(loop_index_testing goat)
add_library(goat_module_execution_support STATIC support/module_execution.c)
target_link_libraries(goat_module_execution_support PUBLIC goat_test_support yyjson)
add_executable(module_execution_testing src/support/module_execution_testing.c)
target_link_libraries(module_execution_testing PRIVATE goat_module_execution_support)
add_dependencies(module_execution_testing module_import_execution_testing)
add_library(goat_inspect_report_support STATIC support/inspect_report.c)
target_link_libraries(goat_inspect_report_support PUBLIC goat_format_oracle goat_common)
add_executable(inspect_report_testing src/support/inspect_report_testing.c)
target_link_libraries(inspect_report_testing PRIVATE goat_inspect_report_support goat_test_support)

# Independent JSON format reference: yyjson is only the syntax/DOM substrate.
# No production module validator or canonicalizer is linked into this oracle.
add_library(goat_format_oracle STATIC support/format_json.c support/format_analysis.c
  support/format_metadata.c support/format_binary.c support/format_zip.c support/format_fixtures.c)
target_link_libraries(goat_format_oracle PUBLIC yyjson goat_zlib m)
add_executable(format_oracle_testing src/support/format_oracle_testing.c)
target_link_libraries(format_oracle_testing PRIVATE goat_format_oracle goat_test_support)
if(UNIX AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_executable(format_oracle_ubsan_testing EXCLUDE_FROM_ALL src/support/format_oracle_testing.c
    support/format_json.c support/format_analysis.c support/format_metadata.c)
  target_link_libraries(format_oracle_ubsan_testing PRIVATE yyjson goat_test_support m)
  target_compile_options(format_oracle_ubsan_testing PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
  set_property(TARGET format_oracle_ubsan_testing APPEND_STRING PROPERTY LINK_FLAGS
    " -fsanitize=undefined -fno-sanitize-recover=all")
endif()

# Independent binary/ZIP reference uses zlib primitives, never Goat format wrappers.
add_executable(format_binary_testing src/support/format_binary_testing.c)
target_link_libraries(format_binary_testing PRIVATE goat_format_oracle goat_test_support)
if(UNIX AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_executable(format_binary_ubsan_testing EXCLUDE_FROM_ALL src/support/format_binary_testing.c
    support/format_json.c support/format_analysis.c support/format_metadata.c
    support/format_binary.c support/format_zip.c support/format_fixtures.c)
  target_link_libraries(format_binary_ubsan_testing PRIVATE yyjson goat_zlib goat_test_support m)
  target_compile_options(format_binary_ubsan_testing PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
  set_property(TARGET format_binary_ubsan_testing APPEND_STRING PROPERTY LINK_FLAGS
    " -fsanitize=undefined -fno-sanitize-recover=all")
endif()


# Independent exact arithmetic belongs only to the test layer, never goat_common.
add_library(goat_integer_oracle STATIC support/integer_oracle.c)
add_executable(integer_oracle_testing src/framework/integer_oracle_testing.c)
target_link_libraries(integer_oracle_testing PRIVATE goat_integer_oracle)
if(UNIX AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # Instrument the oracle itself, not merely its caller.
  add_executable(integer_oracle_ubsan_testing EXCLUDE_FROM_ALL src/framework/integer_oracle_testing.c
    support/integer_oracle.c)
  target_compile_options(integer_oracle_ubsan_testing PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
  set_property(TARGET integer_oracle_ubsan_testing APPEND_STRING PROPERTY LINK_FLAGS
    " -fsanitize=undefined -fno-sanitize-recover=all")
endif()

add_library(goat_test_support STATIC support/scenario.c support/execution.c support/registry.c)
target_link_libraries(goat_test_support PUBLIC goat_common)
add_executable(scenario_context_testing src/framework/scenario_context_testing.c)
target_link_libraries(scenario_context_testing PRIVATE goat_test_support)

add_executable(scenario_execution_testing src/framework/scenario_execution_testing.c)
target_link_libraries(scenario_execution_testing PRIVATE goat_test_support)

add_executable(module_schema_generator_testing src/component/module_schema_generator_testing.c)
target_link_libraries(module_schema_generator_testing PRIVATE goat_test_support)
add_dependencies(module_schema_generator_testing module_schema_generator)

add_executable(unicode_generator_testing src/component/unicode_generator_testing.c)
target_link_libraries(unicode_generator_testing PRIVATE goat_test_support)
add_dependencies(unicode_generator_testing unicode_generator)

add_executable(scenario_registry_testing src/component/scenario_registry_testing.c)
target_link_libraries(scenario_registry_testing PRIVATE goat_test_support)

# Fault hooks are compiled only into this dedicated inspector test host.
add_executable(inspect_probe_testing src/component/inspect_probe_testing.c "${PROJECT_SOURCE_DIR}/src/cli/inspect_probe.c" "${PROJECT_SOURCE_DIR}/lib/process/cleanup_service.c")
target_compile_definitions(inspect_probe_testing PRIVATE GOAT_INSPECT_PROBE_TESTING=1)
target_link_libraries(inspect_probe_testing PRIVATE core goat_validation goat_module m)

file(GLOB unit_testing_exe src/unit/*.c)
message(STATUS "Found unit testing sources: ${unit_testing_exe}")
add_executable(unit_testing ${unit_testing_exe})
target_link_libraries (unit_testing core goat_validation yyjson m)

add_executable(analysis_testing src/analysis/analysis_testing.c)
target_link_libraries(analysis_testing core yyjson m)

add_executable(functional_testing src/functional/functional_testing.c)
target_link_libraries(functional_testing PRIVATE yyjson)

add_executable(module_container_testing src/component/module_container_testing.c)
target_link_libraries(module_container_testing PRIVATE goat_module)

add_executable(zip_testing src/component/zip_testing.c)
target_link_libraries(zip_testing PRIVATE goat_common yyjson)

add_executable(module_json_testing src/component/module_json_testing.c)
target_link_libraries(module_json_testing PRIVATE goat_module)

add_executable(child_process_stream_testing src/component/child_process_stream_testing.c)
target_link_libraries(child_process_stream_testing PRIVATE goat_common)

add_executable(child_process_input_testing src/component/child_process_input_testing.c)
target_link_libraries(child_process_input_testing PRIVATE goat_common)

add_executable(child_process_testing src/component/child_process_testing.c)
target_link_libraries(child_process_testing PRIVATE core m)
if(MINGW)
  set_property(TARGET child_process_testing APPEND_STRING PROPERTY LINK_FLAGS " -municode")
endif()

add_executable(cleanup_service_testing src/component/cleanup_service_testing.c)
target_link_libraries(cleanup_service_testing PRIVATE goat_common m)

add_executable(module_bytecode_testing src/component/module_bytecode_testing.c)
target_link_libraries(module_bytecode_testing PRIVATE core m)

add_executable(module_abi_testing src/component/module_abi_testing.c)
target_link_libraries(module_abi_testing PRIVATE goat_module ${CMAKE_DL_LIBS})

add_executable(module_native_testing src/component/module_native_testing.c)
target_link_libraries(module_native_testing PRIVATE goat_module ${CMAKE_DL_LIBS})

add_executable(module_library_testing src/component/module_library_testing.c)
target_link_libraries(module_library_testing PRIVATE goat_validation m)

add_executable(module_loading_testing src/component/module_loading_testing.c)
target_link_libraries(module_loading_testing PRIVATE goat_validation m)

add_executable(module_dispatch_testing src/component/module_dispatch_testing.c src/component/module_dispatch_identity_testing.c)
target_link_libraries(module_dispatch_testing PRIVATE goat_validation m)

add_executable(module_consistency_testing src/component/module_consistency_testing.c)
target_link_libraries(module_consistency_testing PRIVATE core m)

add_executable(module_validation_testing src/component/module_validation_testing.c)
target_link_libraries(module_validation_testing PRIVATE goat_validation m)

add_executable(module_writer_testing src/component/module_writer_testing.c)
target_link_libraries(module_writer_testing PRIVATE goat_validation m)

add_executable(module_writer_fault_testing src/component/module_writer_fault_testing.c)
target_link_libraries(module_writer_fault_testing PRIVATE goat_validation m)

add_executable(module_sources_testing src/component/module_sources_testing.c)
target_link_libraries(module_sources_testing PRIVATE core m)

add_executable(module_files_testing src/component/module_files_testing.c)
target_link_libraries(module_files_testing PRIVATE core m)

add_executable(module_access_testing src/component/module_access_testing.c src/unit/analysis_test_support.c)
target_link_libraries(module_access_testing PRIVATE core m)

add_executable(module_imports_testing src/component/module_imports_testing.c)
target_link_libraries(module_imports_testing PRIVATE core m)

add_executable(module_search_testing
  src/component/module_search_testing.c
  src/component/module_search_permissions.c)
target_link_libraries(module_search_testing PRIVATE goat_validation m)

add_executable(module_dependencies_testing src/component/module_dependencies_testing.c)
target_link_libraries(module_dependencies_testing PRIVATE goat_validation m)

add_executable(module_graph_testing src/component/module_graph_testing.c)
target_link_libraries(module_graph_testing PRIVATE goat_validation m)

add_executable(module_snapshot_testing src/component/module_snapshot_testing.c)
target_link_libraries(module_snapshot_testing PRIVATE goat_validation m)
add_executable(module_identity_testing src/component/module_identity_testing.c)
target_link_libraries(module_identity_testing PRIVATE goat_validation m)
add_executable(module_initialization_testing src/component/module_initialization_testing.c)
target_link_libraries(module_initialization_testing PRIVATE goat_validation m)
add_executable(module_initialization_errors_testing src/component/module_initialization_errors_testing.c)
target_link_libraries(module_initialization_errors_testing PRIVATE goat_validation m)
add_executable(module_exception_propagation_testing src/component/module_exception_propagation_testing.c)
target_link_libraries(module_exception_propagation_testing PRIVATE goat_validation m)

add_executable(module_initialization_contract_testing src/component/module_initialization_contract_testing.c)
target_link_libraries(module_initialization_contract_testing PRIVATE goat_validation m)
add_executable(module_incomplete_scopes_testing src/component/module_incomplete_scopes_testing.c)
target_link_libraries(module_incomplete_scopes_testing PRIVATE goat_validation m)
add_executable(module_failed_prototypes_testing src/component/module_failed_prototypes_testing.c)
target_link_libraries(module_failed_prototypes_testing PRIVATE goat_validation m)

if(GOAT_TEST_ALLOC_FAILURE)
  add_executable(module_memory_testing src/component/module_memory_testing.c)
  target_link_libraries(module_memory_testing PRIVATE goat_validation m)
endif()

add_executable(source_include_testing src/component/source_include_testing.c)
target_link_libraries(source_include_testing PRIVATE goat_validation m)
add_executable(module_import_execution_testing src/component/module_import_execution_testing.c)
target_link_libraries(module_import_execution_testing PRIVATE goat_validation m)
# Instrument only this acceptance executable, not production libraries. GNU ld
# and lld (including the existing MinGW targets) wrap external call sites.
set_property(TARGET module_import_execution_testing APPEND_STRING PROPERTY LINK_FLAGS
  " -Wl,--wrap=analyze,--wrap=interpret_module,--wrap=run,--wrap=run_module_file")

add_executable(module_serialization_testing src/component/module_serialization_testing.c)
target_link_libraries(module_serialization_testing PRIVATE goat_validation m)
add_library(module_thread_native_fixture SHARED src/component/module_thread_native_fixture.c)
target_include_directories(module_thread_native_fixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
add_executable(module_thread_lifetime_testing src/component/module_thread_lifetime_testing.c)
target_link_libraries(module_thread_lifetime_testing PRIVATE goat_validation m)
target_compile_definitions(module_thread_lifetime_testing PRIVATE LIFETIME_NATIVE_PATH="$<TARGET_FILE:module_thread_native_fixture>")
add_dependencies(module_thread_lifetime_testing module_thread_native_fixture)

add_executable(native_toolchain_testing src/component/native_toolchain_testing.c)
target_link_libraries(native_toolchain_testing PRIVATE core m)
add_executable(native_requirements_testing src/component/native_requirements_testing.c)
target_link_libraries(native_requirements_testing PRIVATE core m)
add_executable(native_cleanup_testing src/component/native_cleanup_testing.c)
target_link_libraries(native_cleanup_testing PRIVATE core goat_validation m)
add_executable(module_native_values_testing src/component/module_native_values_testing.c)
target_link_libraries(module_native_values_testing PRIVATE goat_validation m)
add_executable(native_artifact_testing src/component/native_artifact_testing.c)
target_link_libraries(native_artifact_testing PRIVATE core m)

add_executable(process_exit_testing src/component/process_exit_testing.c)
target_link_libraries(process_exit_testing PRIVATE core goat_validation m)

add_executable(process_exit_failure_testing src/component/process_exit_testing.c "${PROJECT_SOURCE_DIR}/lib/process/process_exit.c")
target_compile_definitions(process_exit_failure_testing PRIVATE GOAT_TEST_PROCESS_EXIT_FAILURE=1)
target_link_libraries(process_exit_failure_testing PRIVATE core goat_validation m)

add_executable(process_exit_delayed_testing src/component/process_exit_testing.c "${PROJECT_SOURCE_DIR}/lib/process/process_exit.c")
target_compile_definitions(process_exit_delayed_testing PRIVATE GOAT_TEST_PROCESS_EXIT_FAILURE=2)
target_link_libraries(process_exit_delayed_testing PRIVATE core goat_validation m)

if(BUILD_TESTING)
  include("${CMAKE_CURRENT_SOURCE_DIR}/CTestLists.cmake")
endif()

add_executable(unicode_interactive_testing src/functional/unicode_interactive_testing.c
  src/functional/characters_cases.c src/functional/unicode_cases.c src/functional/interactive_cases.c)
target_link_libraries(unicode_interactive_testing PRIVATE goat_language_execution_support)
add_dependencies(unicode_interactive_testing goat)

target_compile_definitions(unicode_interactive_testing PRIVATE CHARACTER_CC="${CMAKE_C_COMPILER}" CHARACTER_ROOT="${PROJECT_SOURCE_DIR}")

add_executable(bytecode_v1_testing src/functional/bytecode_v1_testing.c)
target_link_libraries(bytecode_v1_testing PRIVATE goat_language_execution_support goat_format_oracle)
add_dependencies(bytecode_v1_testing goat)

add_executable(module_json_scenarios src/functional/module_json_scenarios.c)
target_link_libraries(module_json_scenarios PRIVATE goat_language_execution_support yyjson)
add_dependencies(module_json_scenarios module_json_testing)

add_executable(module_canonical_hash_scenarios src/functional/module_canonical_hash_scenarios.c)
target_link_libraries(module_canonical_hash_scenarios PRIVATE goat_language_execution_support goat_format_oracle)
add_dependencies(module_canonical_hash_scenarios module_json_testing)
