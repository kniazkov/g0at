:: Copyright 2026 Ivan Kniazkov
:: Distributed under the MIT license; see LICENSE.txt.
@echo off
setlocal
pushd "%~dp0.." || exit /b 1
cmake -S . -B build\release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || goto failed
echo Building Release tools...
cmake -P "%~dp0build_command.cmake" build\release --target goat goat-inspect goat-merge module_delivery_scenarios package_scenarios frozen_samples format_vectors graph_scenarios module_book_scenarios module_delivery_requirements_scenarios module_musl_scenarios || goto failed
echo Building Release package...
cmake -P "%~dp0build_command.cmake" build\release --target package --config Release || goto failed
echo Publishing Release tools...
cmake "-DGOAT_BUILD_DIR=%CD%\build\release" -P "%~dp0publish_tools.cmake" || goto failed
echo.
echo Done.
popd
exit /b 0

:failed
popd
exit /b 1
