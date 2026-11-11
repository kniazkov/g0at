::  Copyright 2026 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
setlocal
set "build_mode=all"
if "%~1"=="--full-check" (
    set "build_mode=full"
    shift /1
)
if "%build_mode%"=="all" if "%~1"=="--build-only" (
    set "build_mode=build"
    shift /1
)
if "%build_mode%"=="all" if "%~1"=="--test-only" (
    set "build_mode=test"
    shift /1
)
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
if not "%~2"=="" goto usage
set "build_jobs=%~1"
if not defined build_jobs set "build_jobs=%CMAKE_BUILD_PARALLEL_LEVEL%"
if not defined build_jobs set "build_jobs=%NUMBER_OF_PROCESSORS%"
if not defined build_jobs set "build_jobs=1"
set build_jobs | findstr /i /r /x "build_jobs=[1-9][0-9]*" > NUL || goto usage
if "%build_mode%"=="full" goto full_check
pushd "%~dp0.." || exit /b 1
if "%build_mode%"=="test" goto tests
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || goto failed
cmake -P "%~dp0build_command.cmake" build --parallel "%build_jobs%" --target process_exit_delayed_testing process_exit_failure_testing process_exit_testing native_artifact_testing native_cleanup_testing module_native_values_testing native_requirements_testing native_toolchain_testing goat goat-inspect goat-merge inspect_probe_testing unit_testing analysis_testing functional_testing module_json_testing child_process_testing child_process_input_testing child_process_stream_testing scenario_context_testing scenario_execution_testing timed_execution_testing scenario_registry_testing module_schema_generator_testing unicode_generator_testing integer_oracle_testing format_oracle_testing format_binary_testing module_execution_testing inspect_report_testing process_watch_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_json_scenarios module_canonical_hash_scenarios cleanup_service_testing zip_scenarios module_container_scenarios module_bytecode_scenarios module_abi_scenarios module_native_scenarios module_sources_scenarios module_files_scenarios module_imports_scenarios source_include_scenarios module_search_scenarios module_dependencies_scenarios module_graph_scenarios module_snapshot_scenarios module_identity_scenarios module_serialization_scenarios module_initialization_scenarios module_initialization_contract_scenarios module_exception_scenarios module_incomplete_scopes_scenarios module_failed_prototypes_scenarios module_access_scenarios module_multifile_access_scenarios module_context_scenarios module_function_provenance_scenarios module_assignment_scenarios module_import_execution_scenarios module_interface_scenarios module_source_diagnostics_scenarios module_information_scenarios module_dispatch_consistency_scenarios module_validation_scenarios module_writer_scenarios module_build_scenarios module_library_scenarios module_loading_scenarios module_cli_scenarios module_inspect_scenarios inspect_probe_scenarios module_merge_scenarios module_delivery_scenarios package_scenarios frozen_samples format_vectors graph_scenarios module_book_scenarios module_delivery_requirements_scenarios module_musl_scenarios child_process_scenarios cleanup_service_scenarios process_exit_scenarios module_exit_scenarios module_threads_scenarios native_requirements_scenarios native_toolchains_scenarios native_artifact_scenarios native_integrity_scenarios native_cleanup_scenarios native_exit_scenarios module_native_values_scenarios native_runner_testing native_runner_policy_testing native_access_scenarios native_libm_scenarios native_runtime_strings_scenarios native_string_method_scenarios native_continuation_testing native_operator_testing module_operator_scenarios native_saturation_scenarios native_performance_scenarios zip_testing module_container_testing module_bytecode_testing module_native_testing module_abi_testing module_dispatch_testing module_loading_testing module_library_testing module_consistency_testing module_validation_testing module_writer_testing module_writer_fault_testing module_sources_testing module_search_testing module_dependencies_testing module_graph_testing module_snapshot_testing module_identity_testing module_initialization_testing module_serialization_testing module_thread_lifetime_testing module_initialization_errors_testing module_exception_propagation_testing module_initialization_contract_testing module_incomplete_scopes_testing module_failed_prototypes_testing source_include_testing module_import_execution_testing module_imports_testing module_files_testing module_access_testing || goto failed
cmake "-DGOAT_BUILD_DIR=%CD%\build" -P "%~dp0publish_tools.cmake" || goto failed
if "%build_mode%"=="build" goto done

:tests
build\module_json_testing.exe || goto failed
build\unit_testing.exe || goto failed
build\analysis_testing.exe test\data\analysis || goto failed

:make_test_work
set "test_work=build\functional-fixtures-%random%-%random%"
if exist "%test_work%" goto make_test_work
mkdir "%test_work%" || goto failed
xcopy /e /i /y /q test\data\functional "%test_work%" >nul || goto failed
pushd "%test_work%"
..\functional_testing.exe ..\goat.exe list.txt
set "testing_result=%errorlevel%"
popd
if not "%testing_result%"=="0" goto failed

:done
echo.
echo Done.
popd
exit /b 0

:failed
popd
exit /b 1

:full_check
cmake "-DGOAT_FULL_JOBS=%build_jobs%" -P "%~dp0full_check.cmake"
exit /b %errorlevel%

:usage
call :help
exit /b 1

:help
echo Usage: %~nx0 [--build-only^|--test-only^|--full-check] [jobs]
echo jobs: positive integer; default: CMAKE_BUILD_PARALLEL_LEVEL or logical CPU count.
echo --full-check: local Debug/Release CI checks; MSYS2 Bash and CMake 3.21+ required.
echo Default: build and test. --test-only requires an existing Debug build.
exit /b 0
