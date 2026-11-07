# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
cmake_minimum_required(VERSION 3.13)

# Use the configured generator, not CMAKE_GENERATOR from the caller's environment.
# Ninja already groups job output. NMake, MSBuild and non-GNU make implementations
# must never receive GNU Make's command-line options.
function(goat_build_command output directory)
  load_cache("${directory}" READ_WITH_PREFIX build_ CMAKE_GENERATOR CMAKE_MAKE_PROGRAM)
  set(command "${CMAKE_COMMAND}" --build "${directory}" ${ARGN})
  if(build_CMAKE_GENERATOR MATCHES "^(Unix|MinGW|MSYS) Makefiles$")
    execute_process(COMMAND "${build_CMAKE_MAKE_PROGRAM}" --version
      RESULT_VARIABLE result OUTPUT_VARIABLE version ERROR_VARIABLE error TIMEOUT 5)
    if(result STREQUAL "0" AND version MATCHES "GNU Make ([0-9]+\\.[0-9]+)"
        AND CMAKE_MATCH_1 VERSION_GREATER_EQUAL "4.0")
      # Keep the original --parallel choice (or CMAKE_BUILD_PARALLEL_LEVEL),
      # targets and configuration. Only add a native output-formatting option.
      list(FIND command "--" separator)
      if(separator EQUAL -1)
        list(APPEND command --)
      endif()
      list(APPEND command --output-sync=target)
    else()
      message(WARNING "${build_CMAKE_GENERATOR}: output synchronization requires GNU Make 4.0 or newer; building without extra make options")
    endif()
  endif()
  set(${output} "${command}" PARENT_SCOPE)
endfunction()

# Both shell families use the same path; full_check.cmake includes the function
# so it can keep recording each native build's result and continue independent stages.
# Usage: cmake -P scripts/build_command.cmake <build-directory> [build options...]
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(CMAKE_ARGC LESS 4)
    message(FATAL_ERROR "Expected a build directory and optional cmake --build arguments")
  endif()
  set(arguments)
  if(CMAKE_ARGC GREATER 4)
    math(EXPR last "${CMAKE_ARGC} - 1")
    foreach(index RANGE 4 ${last})
      list(APPEND arguments "${CMAKE_ARGV${index}}")
    endforeach()
  endif()
  goat_build_command(command "${CMAKE_ARGV3}" ${arguments})
  # Inherit stdout/stderr directly: do not capture, filter or truncate diagnostics.
  execute_process(COMMAND ${command} RESULT_VARIABLE result)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Build failed (${result}): ${CMAKE_ARGV3}")
  endif()
endif()
