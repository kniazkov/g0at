::  Copyright 2025 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
if exist test\%1\program.goat exit /b
mkdir test\%1
copy program.goat test\%1
echo %1 >> test\list.txt
gcc src\functional_testing.c -o functional_testing
cd test
..\functional_testing.exe ..\goat.exe list.txt > NUL
cd ..
for %%F in ("test\%1\actual_output.txt") do if %%~zF NEQ 0 (
    ren test\%1\actual_output.txt expected_output.txt
) else (
    del test\%1\actual_output.txt
)
for %%F in ("test\%1\actual_error.txt") do if %%~zF NEQ 0 (
    ren test\%1\actual_error.txt expected_error.txt
) else (
    del test\%1\actual_error.txt
)
cd test
..\functional_testing.exe ..\goat.exe list.txt
cd ..
del functional_testing.exe
echo.
git add test\%1\*
