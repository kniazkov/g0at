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
cmake -S src -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || goto failed
cmake --build build --parallel "%build_jobs%" --target goat unit_testing analysis_testing functional_testing module_json_testing child_process_testing cleanup_service_testing zip_testing module_container_testing module_bytecode_testing module_native_testing module_abi_testing module_consistency_testing module_validation_testing module_writer_testing module_writer_fault_testing module_sources_testing module_files_testing || goto failed
copy /Y build\goat.exe goat.exe > NUL || goto failed

build\module_json_testing.exe || goto failed
build\unit_testing.exe || goto failed
build\analysis_testing.exe test\analysis || goto failed

pushd test\functional
..\..\build\functional_testing.exe ..\..\goat.exe list.txt
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
