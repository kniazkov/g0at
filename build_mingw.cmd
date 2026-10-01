::  Copyright 2026 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
setlocal
pushd "%~dp0"
cmake -S src -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || goto failed
cmake --build build --target goat unit_testing analysis_testing || goto failed
copy /Y build\goat.exe goat.exe > NUL || goto failed

build\unit_testing.exe || goto failed
build\analysis_testing.exe test\analysis || goto failed

gcc src\functional_testing.c -o build\functional_testing.exe || goto failed
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
