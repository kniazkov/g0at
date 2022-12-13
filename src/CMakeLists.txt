# Copyright 2022 Ivan Kniazkov

# Use of this source code is governed by an MIT-style license
# that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.

project(goat)
cmake_minimum_required(VERSION 3.6)
include_directories(.)
file(GLOB goat_exe main.cpp model/*.cpp)
add_executable(goat ${goat_exe})
file(GLOB unit_testing_exe unit_testing.cpp test/*.cpp model/*.cpp)
add_executable(unit_testing ${unit_testing_exe})
