::  Copyright 2022 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
if exist goat.exe del goat.exe
if not exist build mkdir build
cd build
if exist goat.exe del goat.exe
if exist unit_testing.exe del unit_testing.exe
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..\src

make goat
if exist build\goat.exe copy build\goat.exe .

make unit_testing
echo.
if exist unit_testing.exe unit_testing.exe
cd ..
echo.
echo Done.
