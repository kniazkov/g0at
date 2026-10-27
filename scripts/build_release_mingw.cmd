:: Copyright 2026 Ivan Kniazkov
:: Distributed under the MIT license; see LICENSE.txt.
@echo off
setlocal
pushd "%~dp0.." || exit /b 1
cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || goto failed
cmake --build build\release --target goat goat-inspect goat-merge || goto failed
cmake --build build\release --target package --config Release || goto failed
copy /Y build\release\goat.exe goat.exe > NUL || goto failed
echo.
echo Done.
popd
exit /b 0

:failed
popd
exit /b 1
