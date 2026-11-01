# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Transitional registrations: each existing runner remains one test.
if(DEFINED ENV{BENCHMARK_PYTHON} AND NOT "$ENV{BENCHMARK_PYTHON}" STREQUAL "")
  set(Python3_EXECUTABLE "$ENV{BENCHMARK_PYTHON}")
endif()
option(GOAT_TEST_REQUIRE_PYTHON "Fail configuration if transitional Python tests cannot run" OFF)
if(GOAT_TEST_REQUIRE_PYTHON)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
else()
  find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
find_program(GOAT_TEST_BASH bash)
function(goat_suite name labels)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    LABELS "${labels}" TIMEOUT 600 RUN_SERIAL TRUE
    ENVIRONMENT "CC=${CMAKE_C_COMPILER};PYTHONDONTWRITEBYTECODE=1")
endfunction()
include("${CMAKE_CURRENT_SOURCE_DIR}/ScenarioRegistration.cmake")
function(goat_python name labels script)
  if(Python3_Interpreter_FOUND)
    goat_suite(${name} "${labels}" "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/${script}" ${ARGN})
  else()
    goat_suite(${name} "${labels};unavailable" "${CMAKE_COMMAND}" -E echo "Unavailable: Python 3 is required")
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
  endif()
endfunction()
function(goat_shell name labels script)
  if(GOAT_TEST_BASH)
    goat_suite(${name} "${labels}" "${GOAT_TEST_BASH}" "${PROJECT_SOURCE_DIR}/scripts/${script}" ${ARGN})
  else()
    goat_suite(${name} "${labels};unavailable" "${CMAKE_COMMAND}" -E echo "Unavailable: Bash is required")
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
  endif()
endfunction()
foreach(scenario alive-wait kill deadline close owner-death condition)
  goat_scenario(process.watch.${scenario} "process-watch" TIMEOUT 20 COMMAND
    "$<TARGET_FILE:process_watch_testing>" "${scenario}")
endforeach()
foreach(scenario success mismatch binary-mismatch crlf strict-crlf exit crash timeout cumulative invalid-timeout host-hang host-crash command start-error cleanup-failure barrier barrier-wrong barrier-eof barrier-timeout barrier-after-timeout barrier-action-failure barrier-action-deadline barrier-nested barrier-null barrier-empty scoped-shorter scoped-no-extension scoped-restore scoped-nested scoped-zero scoped-null scoped-expired handshake handshake-wrong handshake-early handshake-timeout handshake-hold-timeout handshake-after-timeout)
  goat_scenario(framework.execution.${scenario} "scenario-execution" TIMEOUT 30 COMMAND "$<TARGET_FILE:scenario_execution_testing>"
    "${scenario}" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
  # Internal deadlines are at most 5 seconds; leave time for process-tree cleanup and artifacts.
  set_tests_properties(framework.execution.${scenario} PROPERTIES TIMEOUT 30)
endforeach()
foreach(scenario success failure keep-work isolation invalid cleanup-error parallel)
  goat_scenario(framework.context.${scenario} "scenario-context" TIMEOUT 30 COMMAND "$<TARGET_FILE:scenario_context_testing>"
    "${scenario}" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
  set_tests_properties(framework.context.${scenario} PROPERTIES TIMEOUT 30)
endforeach()
foreach(scenario roundtrip large poll capture legacy file dialog dialog-poll timeout stop output-error input-error invalid-size invalid-options)
  goat_scenario(process.stream.${scenario} "process-stream" TIMEOUT 20 COMMAND "$<TARGET_FILE:child_process_stream_testing>" "${scenario}")
  set_tests_properties(process.stream.${scenario} PROPERTIES TIMEOUT 20)
endforeach()
foreach(scenario env inherit empty-env empty binary early-close timeout start-error invalid poll stop)
  goat_scenario(process.input.${scenario} "process-input" TIMEOUT 20 COMMAND "$<TARGET_FILE:child_process_input_testing>" "${scenario}")
  set_tests_properties(process.input.${scenario} PROPERTIES TIMEOUT 20)
endforeach()
foreach(scenario validation selection unknown list skip continuation keep-work explicit-skip)
  goat_scenario(framework.registry.${scenario} "scenario-registry" TIMEOUT 30 COMMAND
    "$<TARGET_FILE:scenario_registry_testing>" "${scenario}"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
file(RELATIVE_PATH registry_relative_data "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
goat_scenario(framework.registry.relative-paths "scenario-registry" TIMEOUT 30 COMMAND
  "$<TARGET_FILE:scenario_registry_testing>" selection . "${registry_relative_data}")
set_tests_properties(framework.registry.relative-paths PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
goat_scenario(framework.registry.scheduling "scenario-registry" TIMEOUT 180 COMMAND
  "${CMAKE_COMMAND}" "-DCTEST=${CMAKE_CTEST_COMMAND}" "-DGENERATOR=${CMAKE_GENERATOR}"
  "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
  "-DRUNNER=$<TARGET_FILE:scenario_registry_testing>" "-DBUILD=${CMAKE_BINARY_DIR}"
  "-DDATA=${PROJECT_SOURCE_DIR}/test/data"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/check_scenario_scheduling.cmake")
goat_suite(core.unit "core" $<TARGET_FILE:unit_testing>)
goat_suite(core.json "core;json" $<TARGET_FILE:module_json_testing>)
goat_suite(core.analysis "core;analysis" $<TARGET_FILE:analysis_testing> "${PROJECT_SOURCE_DIR}/test/data/analysis")
goat_suite(core.functional "core;functional" "${CMAKE_COMMAND}"
  "-DGOAT=$<TARGET_FILE:goat>" "-DRUNNER=$<TARGET_FILE:functional_testing>"
  "-DSOURCE=${PROJECT_SOURCE_DIR}/test/data/functional" "-DWORK=${CMAKE_BINARY_DIR}/ctest-functional"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_functional.cmake")
goat_python(integration.module_book "integration" scripts/check_module_book.py "$<TARGET_FILE:goat>")
set(child_process_cases empty exit stdin closed-input inherit parent stdin-open-pipe stdin-file stdin-pty capture-exact capture-over capture-large arguments missing expired stop timeout-pause timeout-continuous timeout-budget timeout-tree crash tree-exit tree-fail tree-crash owner-isolation owner-group-term parent-death launch-death-00 launch-death-01 launch-death-02 launch-death-03 launch-death-04 launch-death-05 launch-death-06 launch-death-07 launch-death-08 launch-death-09 launch-death-10 launch-death-11 launch-death-12 launch-death-13 launch-death-14 launch-death-15 launch-death-16 launch-death-17 launch-death-18 launch-death-19 compiler-probe invalid-timeout-zero invalid-timeout-negative invalid-timeout-decimal invalid-timeout-empty invalid-timeout-plus invalid-timeout-overflow invalid-timeout-huge)
foreach(child_process_case IN LISTS child_process_cases)
  goat_scenario(integration.child_process.${child_process_case} "integration;child-process-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:child_process_scenarios>" "integration.child_process.${child_process_case}"
    "$<TARGET_FILE:child_process_testing>" "$<TARGET_FILE:goat>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
# Keep the independent 120-second scenario deadline; allow two owned cleanup margins.
set_tests_properties(integration.child_process.owner-isolation PROPERTIES TIMEOUT 300)
goat_scenario(integration.zip "integration;zip-migration" TIMEOUT 200 COMMAND
  "$<TARGET_FILE:zip_scenarios>" integration.zip "$<TARGET_FILE:zip_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_container "integration;container-migration" TIMEOUT 200 COMMAND
  "$<TARGET_FILE:module_container_scenarios>" integration.module_container "$<TARGET_FILE:module_container_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_bytecode "integration;module-bytecode-migration" TIMEOUT 200 COMMAND
  "$<TARGET_FILE:module_bytecode_scenarios>" integration.module_bytecode "$<TARGET_FILE:module_bytecode_testing>"
  "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_native "integration;module-abi-native-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_native_scenarios>" integration.module_native "$<TARGET_FILE:module_native_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_abi "integration;module-abi-native-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_abi_scenarios>" integration.module_abi "$<TARGET_FILE:module_abi_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_python(integration.native_artifact "integration" scripts/check_native_artifact.py "$<TARGET_FILE:native_artifact_testing>" "${CMAKE_C_COMPILER}")
goat_python(integration.native_requirements "integration" scripts/check_native_requirements.py "$<TARGET_FILE:native_requirements_testing>")
goat_python(integration.native_toolchains "integration" scripts/check_native_toolchains.py "$<TARGET_FILE:native_toolchain_testing>" "${CMAKE_C_COMPILER}")
goat_python(integration.native_cleanup "integration" scripts/check_native_cleanup.py "$<TARGET_FILE:native_cleanup_testing>" "${CMAKE_C_COMPILER}")
goat_python(integration.module_native_values "integration" scripts/check_module_native_values.py "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:module_native_values_testing>" "${CMAKE_C_COMPILER}")
foreach(module_build_case numeric empty-required empty-off empty-auto-explicit included missing-auto missing-required unsupported-target foreign-target only-dependency)
  goat_scenario(integration.module_build.${module_build_case} "integration;module-build-library-migration" TIMEOUT 390 COMMAND
    "$<TARGET_FILE:module_build_scenarios>" "integration.module_build.${module_build_case}"
    "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_C_COMPILER}"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
foreach(module_library_case original valid missing-descriptor writable-descriptor bad-magic overflow outside-image unterminated different-name missing-last-symbol data-symbol data-in-text valid-sysv data-in-text-sysv object-alias-sysv function-alias-sysv valid-gnu data-in-text-gnu object-alias-gnu function-alias-gnu valid-both data-in-text-both object-alias-both function-alias-both unreadable-descriptor foreign-owner)
  goat_scenario(integration.module_library.${module_library_case} "integration;module-build-library-migration" TIMEOUT 330 COMMAND
    "$<TARGET_FILE:module_library_scenarios>" "integration.module_library.${module_library_case}"
    "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:module_library_testing>" "${CMAKE_C_COMPILER}"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
foreach(module_loading_case off valid-auto valid-required no-match-auto no-match-required plain-auto plain-required foreign-auto foreign-required ambiguous-auto ambiguous-required ambiguous-off damaged-off damaged-auto damaged-required os-auto os-required descriptor-missing-auto descriptor-missing-required descriptor-mismatch-auto descriptor-mismatch-required descriptor-overflow-auto descriptor-overflow-required descriptor-short-auto descriptor-short-required descriptor-writable-auto descriptor-writable-required symbol-missing-auto symbol-missing-required bad-status-auto bad-status-required bad-result-auto bad-result-required write-before-auto write-before-required write-partial-auto write-partial-required cleaner-exit-auto cleaner-exit-required cleaner-silent-auto cleaner-silent-required real-producer-auto real-producer-required unicode allocated-string-auto allocated-string-required static-string-auto static-string-required dormant concurrent-auto concurrent-required dependency-valid-auto dependency-valid-required import-only dependency-only dependency-bytecode dependency-foreign dep-failure-os-auto dep-failure-os-required dep-failure-symbol-missing-auto dep-failure-symbol-missing-required dep-failure-descriptor-mismatch-auto dep-failure-descriptor-mismatch-required foreign-corrupt-dependency-off foreign-corrupt-dependency-auto foreign-corrupt-dependency-required isolated-write-before isolated-write-partial global-exit global-silent integrity-auto integrity-required all-symbols pending-exit-write-auto pending-exit-write-required pending-exit-write-fail-auto pending-exit-write-fail-required concurrent-write-fail concurrent-cleaner-fail caught-auto caught-required frozen continued-auto continued-auto-exit continued-required continued-required-exit late-worker nonreturning-loop-auto nonreturning-loop-required nonreturning-block-auto nonreturning-block-required killed-owner held-auto held-required)
  goat_scenario(integration.module_loading.${module_loading_case} "integration;module-loading-migration" TIMEOUT 330 COMMAND
    "$<TARGET_FILE:module_loading_scenarios>" "integration.module_loading.${module_loading_case}"
    "$<TARGET_FILE:module_loading_testing>" "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_C_COMPILER}"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-inspect/cases.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-inspect/cases.json" module_inspect_ids REGEX "^[ ]+\"id\":")
foreach(module_inspect_line IN LISTS module_inspect_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" module_inspect_id "${module_inspect_line}")
  goat_scenario(integration.module_inspect.${module_inspect_id} "integration;module-inspect-migration" TIMEOUT 210 COMMAND
    "$<TARGET_FILE:module_inspect_scenarios>" "integration.module_inspect.${module_inspect_id}"
    "$<TARGET_FILE:goat-inspect>" "${CMAKE_C_COMPILER}" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1")
endforeach()
set(inspect_probe_cases success capture-0 capture-65536 capture-65537 capture-262144 stdin load-crash load-hang output-hang unload-crash unload-hang load-exit-zero default-timeout descriptor-missing descriptor-mismatch descriptor-overflow symbol-missing load-error explicit-current foreign opposite-os-compatible diamond diamond-foreign first-candidate-current first-candidate-foreign later-candidate-foreign missing-transitive repacked-still-missing repacked-complete invalid-graph text-output fault-start fault-capture fault-unload fault-diagnostic fault-exit-nonzero fault-exit-hang cleaner-start-exit cleaner-start-silent cleanup-timeout-valid cleanup-timeout-load-exit-zero cleanup-timeout-load-crash cleanup-timeout-load-hang owner-killed-before-extraction tree tree-crash tree-hang owner-killed)
foreach(inspect_probe_case IN LISTS inspect_probe_cases)
  goat_scenario(integration.inspect_probe.${inspect_probe_case} "integration;inspect-probe-migration" TIMEOUT 210 COMMAND
    "$<TARGET_FILE:inspect_probe_scenarios>" "integration.inspect_probe.${inspect_probe_case}"
    "$<TARGET_FILE:goat-inspect>" "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_C_COMPILER}"
    "$<TARGET_FILE:inspect_probe_testing>" "$<TARGET_FILE:goat-merge>" "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/docs/module-format-v1")
endforeach()
set(module_cli_cases missing-include basic override-valid native-overrides damaged-native search-precedence cycle descriptor-link direct-source descriptor-context listing listing-link-fail listing-missing listing-path listing-timeout auto-preferred auto-fallback listing-mixed explicit-no-fallback off-no-probe controlled-native descriptor-off default-auto dependency-native-required reports uppercase source-collision hardlink-output directory-output symlink-output legacy)
foreach(index RANGE 0 10)
  list(APPEND module_cli_cases invalid-descriptor-${index})
endforeach()
foreach(index RANGE 0 3)
  list(APPEND module_cli_cases invalid-timeout-${index})
endforeach()
foreach(index RANGE 0 2)
  list(APPEND module_cli_cases raw-valid-${index})
endforeach()
foreach(index RANGE 0 17)
  list(APPEND module_cli_cases raw-invalid-${index})
endforeach()
foreach(module_cli_case IN LISTS module_cli_cases)
  goat_scenario(integration.module_cli.${module_cli_case} "integration;module-cli-migration" TIMEOUT 210 COMMAND
    "$<TARGET_FILE:module_cli_scenarios>" "integration.module_cli.${module_cli_case}"
    "$<TARGET_FILE:goat>" "$<TARGET_FILE:native_toolchain_testing>" "${CMAKE_C_COMPILER}"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
goat_python(integration.module_delivery "integration" scripts/check_module_delivery.py "$<TARGET_FILE:goat>" "$<TARGET_FILE:goat-inspect>" "${CMAKE_C_COMPILER}")
goat_python(integration.module_delivery_requirements "integration" scripts/check_module_delivery_requirements.py "audit" "$<TARGET_FILE:goat>" "$<TARGET_FILE:native_toolchain_testing>" "$<TARGET_FILE:module_native_testing>" "${CMAKE_C_COMPILER}")
goat_python(integration.native_exit "integration" scripts/check_native_exit.py "$<TARGET_FILE:module_dispatch_testing>" "${CMAKE_C_COMPILER}")
set(dispatch_consistency_ids integration.module_dispatch.identity integration.module_consistency.api integration.module_consistency.producer-identities)
foreach(dispatch_consistency_fixture dispatch consistency)
  set(dispatch_consistency_path "${PROJECT_SOURCE_DIR}/test/data/module-dispatch-consistency/${dispatch_consistency_fixture}.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dispatch_consistency_path}")
  file(STRINGS "${dispatch_consistency_path}" dispatch_consistency_lines REGEX "^    \"id\":")
  foreach(dispatch_consistency_line IN LISTS dispatch_consistency_lines)
    string(REGEX REPLACE ".*\"id\": \"([^\"]+)\".*" "\\1" dispatch_consistency_id "${dispatch_consistency_line}")
    list(APPEND dispatch_consistency_ids "${dispatch_consistency_id}")
  endforeach()
endforeach()
foreach(dispatch_consistency_id IN LISTS dispatch_consistency_ids)
  set(dispatch_consistency_timeout 240)
  if(dispatch_consistency_id STREQUAL "integration.module_consistency.producer-identities")
    set(dispatch_consistency_timeout 330)
  endif()
  goat_scenario(${dispatch_consistency_id} "integration;module-dispatch-consistency-migration" TIMEOUT ${dispatch_consistency_timeout} COMMAND
    "$<TARGET_FILE:module_dispatch_consistency_scenarios>" "${dispatch_consistency_id}"
    "$<TARGET_FILE:module_dispatch_testing>" "$<TARGET_FILE:module_consistency_testing>" "$<TARGET_FILE:goat>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
set(validation_ids)
foreach(validation_fixed api file-default file-exact-limit file-archive-limit file-missing constructor.compiler constructor.loader constructor.inspect constructor.merge)
  list(APPEND validation_ids "integration.module_validation.${validation_fixed}")
endforeach()
set(validation_path "${PROJECT_SOURCE_DIR}/test/data/module-validation/scenarios.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${validation_path}")
file(STRINGS "${validation_path}" validation_lines REGEX "^    \"id\":")
foreach(validation_line IN LISTS validation_lines)
  string(REGEX REPLACE ".*\"id\": \"([^\"]+)\".*" "\\1" validation_id "${validation_line}")
  list(APPEND validation_ids "${validation_id}")
endforeach()
foreach(validation_id IN LISTS validation_ids)
  goat_scenario(${validation_id} "integration;module-validation-migration" TIMEOUT 210 COMMAND
    "$<TARGET_FILE:module_validation_scenarios>" "${validation_id}" "$<TARGET_FILE:module_validation_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
set(writer_ids integration.module_writer.api)
foreach(writer_fixed new replace hardlink same-output missing-parent directory symlink fifo fault-write fault-close fault-validation fault-prepare fault-rename)
  list(APPEND writer_ids "integration.module_writer.publication.${writer_fixed}")
endforeach()
set(writer_path "${PROJECT_SOURCE_DIR}/test/data/module-writer/scenarios.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${writer_path}")
file(STRINGS "${writer_path}" writer_lines REGEX "^    \"id\":")
foreach(writer_line IN LISTS writer_lines)
  string(REGEX REPLACE ".*\"id\": \"([^\"]+)\".*" "\\1" writer_id "${writer_line}")
  list(APPEND writer_ids "${writer_id}")
  if(writer_id MATCHES "\\.invalid-|\\.limit-one$")
    string(REPLACE "integration.module_writer." "integration.module_writer.publication." writer_publication "${writer_id}")
    list(APPEND writer_ids "${writer_publication}")
  endif()
endforeach()
foreach(writer_id IN LISTS writer_ids)
  goat_scenario(${writer_id} "integration;module-writer-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_writer_scenarios>" "${writer_id}" "$<TARGET_FILE:module_writer_testing>" "$<TARGET_FILE:module_writer_fault_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
goat_scenario(integration.module_sources "integration;module-sources-files-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_sources_scenarios>" integration.module_sources "$<TARGET_FILE:module_sources_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
goat_scenario(integration.module_search "integration;module-search-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_search_scenarios>" integration.module_search "$<TARGET_FILE:module_search_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_dependencies "integration;module-dependencies-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_dependencies_scenarios>" integration.module_dependencies "$<TARGET_FILE:module_dependencies_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
goat_scenario(integration.module_graph "integration;module-graph-migration" TIMEOUT 660 COMMAND
  "$<TARGET_FILE:module_graph_scenarios>" integration.module_graph "$<TARGET_FILE:module_graph_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
foreach(snapshot_case files replace delete corrupt diamond versions invalid throw deep)
  goat_scenario(integration.module_snapshot.${snapshot_case} "integration;module-snapshot-migration" TIMEOUT 360 COMMAND
    "$<TARGET_FILE:module_snapshot_scenarios>" integration.module_snapshot.${snapshot_case} "$<TARGET_FILE:module_snapshot_testing>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(identity_case identical information corrupt-unused versions vm-first native-first different-native
    corrupt-later fallback-missing extra-fallback mismatching-first hash-conflict)
  goat_scenario(integration.module_identity.${identity_case} "integration;module-identity-serialization-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_identity_scenarios>" integration.module_identity.${identity_case} "$<TARGET_FILE:module_identity_testing>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(initialization_case normal caught early return)
  goat_scenario(integration.module_initialization.${initialization_case} "integration;module-initialization-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_initialization_scenarios>" integration.module_initialization.${initialization_case} "$<TARGET_FILE:module_initialization_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(padding 0 2 7)
  goat_scenario(integration.module_serialization.padding_${padding} "integration;module-identity-serialization-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_serialization_scenarios>" integration.module_serialization.padding_${padding} "$<TARGET_FILE:module_serialization_testing>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(initialization_case object null integer)
  goat_scenario(integration.module_initialization_errors.${initialization_case} "integration;module-initialization-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_initialization_scenarios>" integration.module_initialization_errors.${initialization_case} "$<TARGET_FILE:module_initialization_errors_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(contract_case object null integer success)
  goat_scenario(integration.module_initialization_contract.${contract_case} "integration;module-contract-exceptions-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_initialization_contract_scenarios>" integration.module_initialization_contract.${contract_case} "$<TARGET_FILE:module_initialization_contract_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(rule RANGE 26 35)
  goat_scenario(integration.module_exception_propagation.rule_${rule} "integration;module-contract-exceptions-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_exception_scenarios>" integration.module_exception_propagation.rule_${rule} "$<TARGET_FILE:module_exception_propagation_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(rule RANGE 36 40)
  goat_scenario(integration.module_incomplete_scopes.rule_${rule} "integration;module-scopes-prototypes-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_incomplete_scopes_scenarios>" integration.module_incomplete_scopes.rule_${rule} "$<TARGET_FILE:module_incomplete_scopes_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(shape RANGE 0 3)
  goat_scenario(integration.module_failed_prototypes.shape_${shape} "integration;module-scopes-prototypes-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_failed_prototypes_scenarios>" integration.module_failed_prototypes.shape_${shape} "$<TARGET_FILE:module_failed_prototypes_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
foreach(include_case nested repeat lexical private lazy expression arguments closure-include
    failed-callback same-content cycle-ready cycle-early-caught failed-repeated finally
    throwing-function unicode empty outside invalid hardlinks symlinks diagnostic-mapping deep)
  goat_scenario(integration.source_include.${include_case} "integration;imports-include-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:source_include_scenarios>" integration.source_include.${include_case}
    "$<TARGET_FILE:source_include_testing>" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-import-execution/scenarios.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-import-execution/scenarios.json" execution_ids REGEX "^[ ]+\"id\":")
foreach(execution_line IN LISTS execution_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" execution_id "${execution_line}")
  goat_scenario(${execution_id} "integration;module-execution-interface-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_import_execution_scenarios>" "${execution_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-interface/scenarios.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-interface/scenarios.json" interface_ids REGEX "^[ ]+\"id\":")
foreach(interface_line IN LISTS interface_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" interface_id "${interface_line}")
  goat_scenario(${interface_id} "integration;module-execution-interface-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_interface_scenarios>" "${interface_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data"
    "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
foreach(source_case external-local external-first external-second relocation imported anonymous saved-failure unicode escaped-label frozen)
  goat_scenario(integration.module_source_diagnostics.${source_case} "integration;module-source-diagnostics-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_source_diagnostics_scenarios>" integration.module_source_diagnostics.${source_case} "$<TARGET_FILE:module_import_execution_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
set(process_exit_cases arbiter unprepared prepare-race failed-prepare delayed-prepare fast-three error pipe-error write-stdout write-stderr write-both lock-stdout lock-stderr shared-budget race-00 race-01 race-02 race-03 race-04 race-05 race-06 race-07)
foreach(process_exit_case IN LISTS process_exit_cases)
  goat_scenario(integration.process_exit.${process_exit_case} "integration;process-exit-migration" TIMEOUT 120 COMMAND
    "$<TARGET_FILE:process_exit_scenarios>" "integration.process_exit.${process_exit_case}"
    "$<TARGET_FILE:process_exit_testing>" "$<TARGET_FILE:process_exit_failure_testing>" "$<TARGET_FILE:process_exit_delayed_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
  # Like the original serial runner, don't compete with other tests while measuring
  # the shared flush budget and scheduler-sensitive subsecond fast-exit bound.
  set_tests_properties(integration.process_exit.${process_exit_case} PROPERTIES RUN_SERIAL TRUE)
endforeach()
goat_python(validation.process_exit_original "integration;exit-parity" scripts/check_process_exit.py "$<TARGET_FILE:process_exit_testing>" "$<TARGET_FILE:process_exit_failure_testing>" "$<TARGET_FILE:process_exit_delayed_testing>")
set(module_exit_path "${PROJECT_SOURCE_DIR}/test/data/module-exit/scenarios.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${module_exit_path}")
file(STRINGS "${module_exit_path}" module_exit_lines REGEX "^[ ]+\"id\":")
foreach(module_exit_line IN LISTS module_exit_lines)
  string(REGEX REPLACE ".*\"id\": \"([^\"]+)\".*" "\\1" module_exit_id "${module_exit_line}")
  goat_scenario(${module_exit_id} "integration;module-exit-migration" TIMEOUT 360 COMMAND
    "$<TARGET_FILE:module_exit_scenarios>" "${module_exit_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data/module-exit")
endforeach()
goat_python(validation.module_exit_original "integration;exit-parity" scripts/check_module_exit.py "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>")
goat_python(integration.module_threads "integration" scripts/check_module_threads.py "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>")
# One ID per legacy project or CLI case/optimizer, from the same read-only table.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-assignment/scenarios.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-assignment/scenarios.json" assignment_ids REGEX "^[ ]+\"id\":")
foreach(assignment_line IN LISTS assignment_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" assignment_id "${assignment_line}")
  goat_scenario(${assignment_id} "integration;module-assignment-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_assignment_scenarios>" "${assignment_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
# The immutable fixture table is the single source of stable scenario IDs.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-access/scenarios.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-access/scenarios.json" access_ids REGEX "^[ ]+\"id\":")
foreach(access_line IN LISTS access_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" access_id "${access_line}")
  goat_scenario(${access_id} "integration;module-multifile-access-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_multifile_access_scenarios>" "${access_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
goat_scenario(integration.module_imports "integration;imports-include-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_imports_scenarios>" integration.module_imports "$<TARGET_FILE:module_imports_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
goat_scenario(integration.module_files "integration;module-sources-files-migration" TIMEOUT 240 COMMAND
  "$<TARGET_FILE:module_files_scenarios>" integration.module_files "$<TARGET_FILE:module_files_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
foreach(access_case model_vm wire_files wire_declarations)
  goat_scenario(integration.module_access.${access_case} "integration;module-access-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_access_scenarios>" integration.module_access.${access_case} "$<TARGET_FILE:module_access_testing>"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
set(information_ids integration.module_information.no-network-symbols)
foreach(information_fixture information information_merge)
  set(information_path "${PROJECT_SOURCE_DIR}/test/data/module-information/${information_fixture}.json")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${information_path}")
  file(STRINGS "${information_path}" information_lines REGEX "^[ \t]*\"id\":")
  foreach(information_line IN LISTS information_lines)
    string(REGEX REPLACE ".*\"id\": \"([^\"]+)\".*" "\\1" information_id "${information_line}")
    list(APPEND information_ids "${information_id}")
  endforeach()
endforeach()
foreach(information_id IN LISTS information_ids)
  goat_scenario(${information_id} "integration;module-information-migration" TIMEOUT 240 COMMAND
    "$<TARGET_FILE:module_information_scenarios>" "${information_id}"
    "$<TARGET_FILE:module_json_testing>" "$<TARGET_FILE:module_writer_testing>" "${CMAKE_NM}"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data"
    "${PROJECT_SOURCE_DIR}/docs/module-format-v1/vectors")
endforeach()
include("${PROJECT_SOURCE_DIR}/test/data/module-merge/case-ids.cmake")
foreach(module_merge_case IN LISTS module_merge_case_ids)
  goat_scenario(integration.module_merge.${module_merge_case} "integration;module-merge-migration" TIMEOUT 120 COMMAND
    "$<TARGET_FILE:module_merge_scenarios>" "integration.module_merge.${module_merge_case}"
    "$<TARGET_FILE:goat-merge>" "$<TARGET_FILE:goat-inspect>" "${CMAKE_C_COMPILER}"
    "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/docs/module-format-v1")
endforeach()
goat_scenario(native.module_json "integration;metadata-json" TIMEOUT 200 COMMAND
  "$<TARGET_FILE:module_json_scenarios>" native.module_json "$<TARGET_FILE:module_json_testing>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
foreach(kind canonical hash)
  goat_scenario(native.module_${kind} "integration;canonical-hash" TIMEOUT 200 COMMAND
    "$<TARGET_FILE:module_canonical_hash_scenarios>" native.module_${kind} "$<TARGET_FILE:module_json_testing>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
# Original/C parity confirmed on all target configurations before Python removal (#376).
set(cleanup_service_cases channel retry lazy startup-missing startup-exit startup-silent startup-late normal owner-quit owner-exit detached-client invalid-native-name killed-owner dead-cleaner-0 dead-cleaner-23 active-compiler child-deadline cleanup-race-00 cleanup-race-01 cleanup-race-02 cleanup-race-03 cleanup-race-04 cleanup-race-05 cleanup-race-06 cleanup-race-07 launch-race-00 launch-race-01 launch-race-02 launch-race-03 launch-race-04 launch-race-05 launch-race-06 launch-race-07 busy permanently-busy parallel-lifetime)
foreach(cleanup_service_case IN LISTS cleanup_service_cases)
  goat_scenario(integration.cleanup_service.${cleanup_service_case} "integration;process;cleanup-service-migration" TIMEOUT 180 COMMAND
    "$<TARGET_FILE:cleanup_service_scenarios>" "integration.cleanup_service.${cleanup_service_case}"
    "$<TARGET_FILE:cleanup_service_testing>" "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
set_tests_properties(integration.cleanup_service.parallel-lifetime PROPERTIES TIMEOUT 360)

goat_python(format.reference "format" docs/module-format-v1/test_reference.py)
goat_scenario(format.schema "format;module-schema" TIMEOUT 40 COMMAND
  "$<TARGET_FILE:module_schema_generator_testing>" check "$<TARGET_FILE:module_schema_generator>"
  "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
foreach(scenario roundtrip stale missing reject-keyword reject-pattern reject-type reject-ref reject-float reject-duplicate
    reject-array-items reject-ref-siblings reject-zero-items reject-zero-length reject-unique-nonstrings reject-ref-cycle)
  goat_scenario(format.schema.${scenario} "format;module-schema" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:module_schema_generator_testing>" "${scenario}" "$<TARGET_FILE:module_schema_generator>"
    "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
endforeach()
foreach(scenario check roundtrip stale missing missing-input reject-unicode reject-special reject-property)
  goat_scenario(unicode.generator.${scenario} "unicode-generator" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:unicode_generator_testing>" "${scenario}" "$<TARGET_FILE:unicode_generator>"
    "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
endforeach()
if(GOAT_TEST_ALLOC_FAILURE)
  foreach(memory_case baseline persistent once after-last boundaries add-overflow multiply-overflow limit-exact limit-archive limit-root-expanded limit-dependency-expanded count20 count24 count28 count32 count36 declared-expanded)
    goat_scenario(fault.module_memory.${memory_case} "fault;module-memory-migration" TIMEOUT 330 COMMAND
      "$<TARGET_FILE:module_memory_scenarios>" "fault.module_memory.${memory_case}" "$<TARGET_FILE:module_memory_testing>"
      "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
  endforeach()
endif()
goat_scenario(native.bytecode_v1 "native;standalone-bytecode" TIMEOUT 200 COMMAND
  "$<TARGET_FILE:bytecode_v1_testing>" native.bytecode_v1 "$<TARGET_FILE:goat>"
  "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
goat_python(native.native_integrity "native" scripts/check_native_integrity.py $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/native_integrity")
foreach(scenario native.for native.index)
  goat_scenario(${scenario} "native;language-roundtrip" TIMEOUT 200 COMMAND
    "$<TARGET_FILE:loop_index_testing>" "${scenario}" "$<TARGET_FILE:goat>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
goat_python(native.native "native" scripts/check_native.py $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/native")
if(NOT WIN32)
goat_shell(native.native_library "native" check_native_library.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/native_library")
endif()
goat_shell(native.native_loader "native" check_native_loader.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/native_loader")
goat_shell(native.native_call "native" check_native_call.sh $<TARGET_FILE:goat> $<TARGET_FILE:unit_testing> "${CMAKE_BINARY_DIR}/ctest/native_call")
goat_shell(native.native_pipeline "native" check_native_pipeline.sh $<TARGET_FILE:goat> $<TARGET_FILE:unit_testing> "${CMAKE_BINARY_DIR}/ctest/native_pipeline")
goat_shell(native.native_modes "native" check_native_modes.sh $<TARGET_FILE:goat> $<TARGET_FILE:functional_testing> "${CMAKE_BINARY_DIR}/ctest/native_modes")
goat_shell(native.binary_program "native" check_binary_program.sh $<TARGET_FILE:goat> $<TARGET_FILE:functional_testing> "${CMAKE_BINARY_DIR}/ctest/binary_program")
goat_shell(native.native_abi "native" check_native_abi.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/native_abi")
goat_shell(native.c_module "native" check_c_module.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/c_module")
goat_shell(native.c_generation "native" check_c_generation.sh $<TARGET_FILE:unit_testing> "${CMAKE_BINARY_DIR}/ctest/c_generation")
goat_python(performance.native "performance" scripts/check_native_performance.py $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/performance")
set_tests_properties(performance.native PROPERTIES TIMEOUT 1200)

# These variants match the existing dedicated CI steps, not replacement coverage.
if(WIN32)
  goat_shell(native.windows_library "native;windows" check_native_windows.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/windows-library")
  goat_shell(native.windows_target "native;windows" check_windows_target.sh "${CMAKE_BINARY_DIR}/ctest/windows-target")
else()
  goat_python(sanitizer.native_strings "sanitizer" test/src/native/checks/runtime_strings.py $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/strings-ubsan" -fsanitize=undefined -fno-sanitize-recover=all)
  goat_shell(sanitizer.native_abi "sanitizer" check_native_abi.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/abi-ubsan" -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all)
  goat_shell(sanitizer.c_generation "sanitizer" check_c_generation.sh $<TARGET_FILE:unit_testing> "${CMAKE_BINARY_DIR}/ctest/c-ubsan" -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    goat_shell(native.abi_x87 "native;x87" check_native_abi.sh $<TARGET_FILE:goat> "${CMAKE_BINARY_DIR}/ctest/abi-x87" -mfpmath=387)
    goat_shell(native.c_generation_x87 "native;x87" check_c_generation.sh $<TARGET_FILE:unit_testing> "${CMAKE_BINARY_DIR}/ctest/c-x87" -mfpmath=387)
  endif()
endif()
goat_python(package.relocated "package" scripts/check_package.py "${CMAKE_BINARY_DIR}/packages")
# Packaging is an explicit prerequisite; ordinary builds do not create archives.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/packages")
  set_tests_properties(package.relocated PROPERTIES DISABLED TRUE)
  message(STATUS "CTest package.relocated unavailable: build the package target, then reconfigure")
endif()
find_program(GOAT_TEST_FORMAT NAMES clang-format-18)
find_program(GOAT_TEST_DOXYGEN doxygen)
find_program(GOAT_TEST_DOT dot)
goat_shell(tool.style "style" check_style.sh)
goat_shell(tool.docs "docs" check_docs.sh)
goat_shell(tool.graph "graph" check_graph.sh)
set_tests_properties(tool.graph PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER};PYTHONDONTWRITEBYTECODE=1;GOAT_BINARY=$<TARGET_FILE:goat>")
foreach(pair "style;GOAT_TEST_FORMAT" "docs;GOAT_TEST_DOXYGEN" "graph;GOAT_TEST_DOT")
  list(GET pair 0 suite)
  list(GET pair 1 tool)
  if(NOT ${tool})
    set_tests_properties(tool.${suite} PROPERTIES DISABLED TRUE)
    message(STATUS "CTest tool.${suite} unavailable: missing ${tool}")
  endif()
endforeach()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  set_tests_properties(performance.native PROPERTIES DISABLED TRUE)
  message(STATUS "CTest performance.native unavailable: configure a Release build")
endif()
find_program(GOAT_TEST_PYTHON3_COMMAND python3)
if(NOT GOAT_TEST_PYTHON3_COMMAND)
  set_tests_properties(tool.graph PROPERTIES DISABLED TRUE)
  message(STATUS "CTest tool.graph unavailable: existing shell runner requires python3 on PATH")
endif()

set(context_cases same-image file-origin reexport relocation unicode canonical-direct canonical-alias frozen-direct frozen-alias source-direct source-alias native-alias)
foreach(constant main dir)
  foreach(form RANGE 0 11)
    list(APPEND context_cases reserved-${constant}-${form})
  endforeach()
endforeach()
foreach(context_case IN LISTS context_cases)
  goat_scenario(integration.module_context_constants.${context_case} "integration;module-context-provenance-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_context_scenarios>" integration.module_context_constants.${context_case}
    "$<TARGET_FILE:module_import_execution_testing>" "$<TARGET_FILE:goat>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/src/support/context_retarget_compiler.c")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/test/data/module-provenance/scenarios.json")
file(STRINGS "${PROJECT_SOURCE_DIR}/test/data/module-provenance/scenarios.json" provenance_ids REGEX "^[ ]+\"id\":")
foreach(provenance_line IN LISTS provenance_ids)
  string(REGEX REPLACE "^[ ]+\"id\": \"([^\"]+)\",?$" "\\1" provenance_id "${provenance_line}")
  goat_scenario(${provenance_id} "integration;module-context-provenance-migration" TIMEOUT 150 COMMAND
    "$<TARGET_FILE:module_function_provenance_scenarios>" "${provenance_id}"
    "$<TARGET_FILE:module_import_execution_testing>" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()

# This count protects the transitional CI replacement against dropped registrations.
get_property(goat_registered_suites DIRECTORY PROPERTY TESTS)
set(goat_integration_count 0)
foreach(suite IN LISTS goat_registered_suites)
  if(suite MATCHES "^integration\\.")
    math(EXPR goat_integration_count "${goat_integration_count} + 1")
  endif()
endforeach()
if(NOT goat_integration_count EQUAL 1559)
  message(FATAL_ERROR "Expected all 1559 integration scenarios (exit cases split in #377); found ${goat_integration_count}")
endif()
# Read-only provenance/reproduction check; ordinary consumers never generate inputs.
goat_python(tool.python-frozen-samples "tool;frozen-samples" "test/support/freeze_python_samples.py" --check)
goat_python(tool.python-frozen-samples-self-test "tool;frozen-samples" "test/support/freeze_python_samples.py" --self-test)

# #334: exact Python parity and deliberate corruptions of the independent oracle.
foreach(scenario parity reject-value reject-sign reject-clamp)
  goat_scenario(framework.integer-oracle.${scenario} "integer-oracle" TIMEOUT 30 COMMAND
    "$<TARGET_FILE:integer_oracle_testing>" "${scenario}"
    "${PROJECT_SOURCE_DIR}/test/data/integer-oracle/python-results.tsv")
endforeach()
if(TARGET integer_oracle_ubsan_testing)
  goat_scenario(framework.integer-oracle.ubsan "integer-oracle;sanitizer" TIMEOUT 30 COMMAND
    "$<TARGET_FILE:integer_oracle_ubsan_testing>" parity
    "${PROJECT_SOURCE_DIR}/test/data/integer-oracle/python-results.tsv")
endif()

# #335: independent schema-driven JSON oracle; Python reference remains for consumers.
foreach(scenario strict_json canonical_bytes shapes gmod constants facts_normalization summaries summary_consistency dependency_versions hash_projection target guard_intersections normalization_preserves_distinctions hash_excludes_native_but_validates_it native_envelope edge_cases vectors metadata_edges target_edges guard_edges)
  goat_scenario(format.oracle.${scenario} "format-oracle" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_oracle_testing>" "${scenario}" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
foreach(mutant corrupt-output accept-invalid)
  goat_scenario(format.oracle.reject-${mutant} "format-oracle" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_oracle_testing>" all "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json"
    "${mutant}")
  set_tests_properties(format.oracle.reject-${mutant} PROPERTIES WILL_FAIL TRUE)
endforeach()
if(TARGET format_oracle_ubsan_testing)
  goat_scenario(format.oracle.ubsan "format-oracle;sanitizer" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_oracle_ubsan_testing>" all "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endif()

# #336: independent bytecode, ZIP, hash and declaration fixtures; read-only goldens.
foreach(scenario bytecode modules fixtures vectors zip)
  goat_scenario(format.binary.${scenario} "format-oracle;format-binary" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_binary_testing>" "${scenario}" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endforeach()
foreach(mutant corrupt-output accept-invalid)
  goat_scenario(format.binary.reject-${mutant} "format-oracle;format-binary" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_binary_testing>" all "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json" "${mutant}")
  set_tests_properties(format.binary.reject-${mutant} PROPERTIES WILL_FAIL TRUE)
endforeach()
if(TARGET format_binary_ubsan_testing)
  goat_scenario(format.binary.ubsan "format-oracle;format-binary;sanitizer" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:format_binary_ubsan_testing>" all "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/docs/module-format-v1/schema-source.json")
endif()

# Stable scenario IDs for the shared execution/report migration (#337).
foreach(scenario parity parsing)
  goat_scenario(helpers.inspect.${scenario} "migration-helpers" TIMEOUT 40 COMMAND
    "$<TARGET_FILE:inspect_report_testing>" "${scenario}" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data/inspect-report/cases.json"
    "${PROJECT_SOURCE_DIR}/docs/module-format-v1/inspect-report.schema.json"
    "${PROJECT_SOURCE_DIR}/docs/module-format-v1")
endforeach()
goat_scenario(helpers.inspect.reject-mutant "migration-helpers" TIMEOUT 40 COMMAND
  "$<TARGET_FILE:inspect_report_testing>" parity "${CMAKE_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}/test/data" "${PROJECT_SOURCE_DIR}/test/data/inspect-report/cases.json"
  "${PROJECT_SOURCE_DIR}/docs/module-format-v1/inspect-report.schema.json"
  "${PROJECT_SOURCE_DIR}/docs/module-format-v1" accept-invalid)
set_tests_properties(helpers.inspect.reject-mutant PROPERTIES WILL_FAIL TRUE)
foreach(scenario real compile-failure capture environment mismatch expected-crlf invalid-utf8 missing-diagnostic unexpected-success unexpected-failure invalid-path)
  goat_scenario(helpers.execution.${scenario} "migration-helpers" TIMEOUT 90 COMMAND
    "$<TARGET_FILE:module_execution_testing>" "${scenario}" "${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/test/data" "$<TARGET_FILE:module_import_execution_testing>")
endforeach()

foreach(scenario native.characters native.unicode native.interactive)
  goat_scenario(${scenario} "native;unicode-interactive" TIMEOUT 200 COMMAND
    "$<TARGET_FILE:unicode_interactive_testing>" "${scenario}" "$<TARGET_FILE:goat>"
    "${CMAKE_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/test/data")
endforeach()
