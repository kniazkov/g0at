# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

cmake_minimum_required(VERSION 3.13)
project(goat LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Keep the executable paths consumed by scripts and packages stable while the
# source directories are split into separate build descriptions.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_subdirectory(src)
include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/package.cmake")
