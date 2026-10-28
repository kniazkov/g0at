::  Copyright 2026 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
setlocal
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
if not "%~2"=="" goto usage
set "build_jobs=%~1"
if not defined build_jobs set "build_jobs=%CMAKE_BUILD_PARALLEL_LEVEL%"
if not defined build_jobs set "build_jobs=%NUMBER_OF_PROCESSORS%"
if not defined build_jobs set "build_jobs=1"
set build_jobs | findstr /i /r /x "build_jobs=[1-9][0-9]*" > NUL || goto usage
pushd "%~dp0.." || exit /b 1
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || goto failed
cmake --build build --parallel "%build_jobs%" --target process_exit_delayed_testing process_exit_failure_testing process_exit_testing native_artifact_testing native_cleanup_testing module_native_values_testing native_requirements_testing native_toolchain_testing goat goat-inspect goat-merge inspect_probe_testing unit_testing analysis_testing functional_testing module_json_testing child_process_testing child_process_input_testing child_process_stream_testing scenario_context_testing scenario_execution_testing scenario_registry_testing module_schema_generator_testing unicode_generator_testing integer_oracle_testing format_oracle_testing format_binary_testing module_execution_testing inspect_report_testing process_watch_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_json_scenarios module_canonical_hash_scenarios cleanup_service_testing zip_testing module_container_testing module_bytecode_testing module_native_testing module_abi_testing module_dispatch_testing module_loading_testing module_library_testing module_consistency_testing module_validation_testing module_writer_testing module_writer_fault_testing module_sources_testing module_search_testing module_dependencies_testing module_graph_testing module_snapshot_testing module_identity_testing module_initialization_testing module_serialization_testing module_thread_lifetime_testing module_initialization_errors_testing module_exception_propagation_testing module_initialization_contract_testing module_incomplete_scopes_testing module_failed_prototypes_testing source_include_testing module_import_execution_testing module_imports_testing module_files_testing module_access_testing || goto failed
copy /Y build\goat.exe goat.exe > NUL || goto failed

build\module_json_testing.exe || goto failed
build\unit_testing.exe || goto failed
build\analysis_testing.exe test\data\analysis || goto failed

:make_test_work
set "test_work=build\functional-fixtures-%random%-%random%"
if exist "%test_work%" goto make_test_work
mkdir "%test_work%" || goto failed
xcopy /e /i /y /q test\data\functional "%test_work%" >nul || goto failed
pushd "%test_work%"
..\functional_testing.exe ..\..\goat.exe list.txt
set "testing_result=%errorlevel%"
popd
if not "%testing_result%"=="0" goto failed

echo.
echo Done.
popd
exit /b 0

:failed
popd
exit /b 1

:usage
call :help
exit /b 1

:help
echo Usage: %~nx0 [jobs]
echo jobs: positive integer; default: CMAKE_BUILD_PARALLEL_LEVEL or logical CPU count.
exit /b 0
