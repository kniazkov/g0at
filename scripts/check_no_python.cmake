# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Dependency audit, not a security sandbox or a parser for arbitrary obfuscation.
# Historical comments, frozen data and rejected .py fixture identifiers are not
# executable routes. Inspect all project orchestration/C source plus generated
# CTest commands and their environment properties, including absolute paths.
cmake_minimum_required(VERSION 3.13)
if(NOT DEFINED SOURCE)
  get_filename_component(SOURCE "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT DEFINED INPUTS)
  file(GLOB_RECURSE INPUTS
    "${SOURCE}/scripts/*.ps1" "${SOURCE}/scripts/*.sh" "${SOURCE}/scripts/*.cmd" "${SOURCE}/scripts/*.bat" "${SOURCE}/scripts/*.cmake"
    "${SOURCE}/.github/workflows/*.yml" "${SOURCE}/.github/workflows/*.yaml"
    "${SOURCE}/src/*.c" "${SOURCE}/src/*.h" "${SOURCE}/src/*.cmake" "${SOURCE}/src/CMakeLists.txt"
    "${SOURCE}/lib/*.c" "${SOURCE}/lib/*.h" "${SOURCE}/lib/*.cmake" "${SOURCE}/lib/CMakeLists.txt"
    "${SOURCE}/tools/*.c" "${SOURCE}/tools/*.h" "${SOURCE}/tools/*.cmake" "${SOURCE}/tools/CMakeLists.txt"
    "${SOURCE}/test/*.cmake" "${SOURCE}/test/CMakeLists.txt"
    "${SOURCE}/test/src/*.c" "${SOURCE}/test/src/*.h"
    "${SOURCE}/test/support/*.c" "${SOURCE}/test/support/*.h")
  list(APPEND INPUTS "${SOURCE}/CMakeLists.txt")
  file(GLOB_RECURSE recipes "${SOURCE}/scripts/*.py" "${SOURCE}/tools/*.py"
    "${SOURCE}/test/support/*.py" "${SOURCE}/test/src/*.py")
  if(recipes)
    message(FATAL_ERROR "Python recipes remain in executable project directories: ${recipes}")
  endif()
  foreach(build "${SOURCE}/build" ${BUILDS})
    file(GLOB_RECURSE generated "${build}/CTestTestfile.cmake")
    list(APPEND INPUTS ${generated})
  endforeach()
endif()
set(interpreter "(pythonw?|pypyw?)([0-9]+([.][0-9]+)*)?|pyw?")
set(count 0)
foreach(path IN LISTS INPUTS)
  # Only the proof implementation may name forbidden commands. Its negative
  # controls exercise these exceptions; production sources have no exception.
  if(path MATCHES "/scripts/(no_python|check_no_python|check_python_absence)[.](sh|ps1|cmake)$"
      OR path MATCHES "/test/support/python_forbidden[.]c$")
    continue()
  endif()
  file(READ "${path}" content)
  if(content MATCHES "(^|\n)#![^\r\n]*(python|pypy)")
    message(FATAL_ERROR "Python shebang in ${path}")
  endif()
  if(path MATCHES "[.](c|h)$")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" content "${content}")
    string(REGEX REPLACE "//[^\r\n]*" "" content "${content}")
    string(TOLOWER "${content}" content)
    # Quoted executable/command strings, including absolute Windows/POSIX paths.
    # Keep these alternatives separate: older supported CMake regex engines
    # permit at most nine capture groups in one expression (seven/five here).
    set(pattern "[\"']([^\"'\r\n]*[/\\\\])?(${interpreter})([.]exe)?([\"']|[ \t]+[-/])")
    set(absolute_command "[\"'][^\"'\r\n]*[/\\\\](${interpreter})([.]exe)?[ \t]")
    if(content MATCHES "${absolute_command}")
      message(FATAL_ERROR "Python interpreter route in ${path}: ${CMAKE_MATCH_0}")
    endif()
  else()
    string(REGEX REPLACE "(^|\n)[ \t]*(#|::|[Rr][Ee][Mm] )[^\r\n]*" "\n" content "${content}")
    string(TOLOWER "${content}" content)
    set(pattern "(^|[ \t\r\n;|&\"'(/\\\\])(${interpreter})([.]exe)?($|[ \t\r\n;|&\"')])")
    if(content MATCHES "python[a-z0-9_]*_executable[ \t]*[:=]" OR content MATCHES "[.]py([ \t\r\n\"';)]|$)")
      message(FATAL_ERROR "Python execution/configuration route in ${path}")
    endif()
  endif()
  if(content MATCHES "${pattern}")
    message(FATAL_ERROR "Python interpreter route in ${path}: ${CMAKE_MATCH_0}")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
message(STATUS "Python-free source/CTest command audit: ${count} files")
