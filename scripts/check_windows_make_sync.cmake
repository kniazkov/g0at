# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
cmake_minimum_required(VERSION 3.20)
# Diagnostic only: observe, do not wait on or modify inherited sync objects.
if(NOT WIN32)
  message(FATAL_ERROR "This probe requires native Windows CMake and GNU Make")
endif()
# Isolate top-level controls; tested descendants inherit normally.
unset(ENV{MAKEFLAGS})
unset(ENV{MFLAGS})
unset(ENV{GNUMAKEFLAGS})
find_program(PROBE_CC NAMES gcc cc REQUIRED)
find_program(PROBE_MAKE NAMES mingw32-make REQUIRED)
if(NOT DEFINED PROBE_SOURCE)
  set(PROBE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/windows_make_sync_probe.c")
endif()
if(NOT DEFINED PROBE_DIR)
  set(PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/build/ci/windows-make-sync")
endif()
file(MAKE_DIRECTORY "${PROBE_DIR}")
file(WRITE "${PROBE_DIR}/probe.log" "Windows Make sync handle diagnostic\n")
function(run_probe label)
  execute_process(COMMAND ${ARGN}
    WORKING_DIRECTORY "${PROBE_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
  file(APPEND "${PROBE_DIR}/probe.log"
    "\n=== ${label} ===\n${out}${err}\nresult=${result}\n")
  message(STATUS "${label}:\n${out}${err}\nresult=${result}")
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Diagnostic command ${label} failed: ${result}; see ${PROBE_DIR}/probe.log")
  endif()
endfunction()
run_probe(compiler_version "${PROBE_CC}" --version)
run_probe(make_version "${PROBE_MAKE}" --version)
run_probe(cmake_version "${CMAKE_COMMAND}" --version)
run_probe(compile "${PROBE_CC}" "${PROBE_SOURCE}" -O0 -g -Wall -Wextra -o probe.exe)
run_probe(self_control "${PROBE_DIR}/probe.exe" self_control)
file(WRITE "${PROBE_DIR}/child.cmake" [=[
execute_process(COMMAND "${CMAKE_CURRENT_LIST_DIR}/probe.exe" execute_process
  RESULT_VARIABLE result TIMEOUT 10)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "probe failed: ${result}")
endif()
]=])
file(WRITE "${PROBE_DIR}/Makefile" [=[
.PHONY: all direct env execute recursive
all: direct env execute recursive
direct:
	./probe.exe direct $(EXPECT_DIRECT)
env:
	"$(PROBE_CMAKE)" -E env ./probe.exe cmake_env
execute:
	"$(PROBE_CMAKE)" -P child.cmake
recursive:
	$(MAKE) -f child.mk
]=])
file(WRITE "${PROBE_DIR}/child.mk" [=[
.PHONY: child
child:
	./probe.exe recursive_make
]=])
foreach(mode none target recurse)
  if(mode STREQUAL "none")
    set(expect absent)
  else()
    set(expect mutex)
  endif()
  foreach(target direct env execute recursive)
    run_probe("output_sync_${mode}_${target}" "${PROBE_MAKE}" -j4 "--output-sync=${mode}"
      "PROBE_CMAKE=${CMAKE_COMMAND}" "EXPECT_DIRECT=${expect}" "${target}")
  endforeach()
endforeach()
message(STATUS "Probe completed. Handle validity/type differences are observations, not assertions of the original hang's cause.")
