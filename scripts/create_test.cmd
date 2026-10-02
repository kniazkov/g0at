::  Copyright 2025 Ivan Kniazkov

::  Use of this source code is governed by an MIT-style license
::  that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

@echo off
setlocal
pushd "%~dp0.." || exit /b 1
if exist test\functional\%1\program.goat goto done
mkdir test\functional\%1
copy program.goat test\functional\%1
echo %1 >> test\functional\list.txt
gcc src\functional_testing.c -o functional_testing
cd test\functional
..\..\functional_testing.exe ..\..\goat.exe list.txt > NUL
cd ..\..
for %%F in ("test\functional\%1\actual_output_all.txt") do if %%~zF NEQ 0 (
    ren test\functional\%1\actual_output_all.txt expected_output.txt
) else (
    del test\functional\%1\actual_output_all.txt
)
for %%F in ("test\functional\%1\actual_error_all.txt") do if %%~zF NEQ 0 (
    ren test\functional\%1\actual_error_all.txt expected_error.txt
) else (
    del test\functional\%1\actual_error_all.txt
)
cd test\functional
..\..\functional_testing.exe ..\..\goat.exe list.txt
cd ..\..
del functional_testing.exe
echo.
git add test\functional\%1\*

:done
popd
endlocal
