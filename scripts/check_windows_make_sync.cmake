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
# Package provenance matters: MSYS2 may link system libuv rather than use
# CMake's bundled copy. Optional probes never hide a usable diagnostic.
find_program(PROBE_PACMAN NAMES pacman)
find_program(PROBE_LDD NAMES ldd)
foreach(tool PACMAN LDD)
  if(PROBE_${tool})
    if(tool STREQUAL "PACMAN")
      set(provenance_args -Q)
    else()
      set(provenance_args "${CMAKE_COMMAND}")
    endif()
    execute_process(COMMAND "${PROBE_${tool}}" ${provenance_args}
      RESULT_VARIABLE provenance_result OUTPUT_VARIABLE provenance_out
      ERROR_VARIABLE provenance_err TIMEOUT 15)
    file(APPEND "${PROBE_DIR}/probe.log"
      "\n=== provenance_${tool} ===\n${provenance_out}${provenance_err}\nresult=${provenance_result}\n")
  else()
    file(APPEND "${PROBE_DIR}/probe.log" "\nprovenance_${tool}=unavailable\n")
  endif()
endforeach()
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
  if(mode STREQUAL "target")
    set(expect mutex)
  else()
    set(expect absent)
  endif()
  foreach(target direct env execute recursive)
    run_probe("output_sync_${mode}_${target}" "${PROBE_MAKE}" -j4 "--output-sync=${mode}"
      "PROBE_CMAKE=${CMAKE_COMMAND}" "EXPECT_DIRECT=${expect}" "${target}")
  endforeach()
endforeach()
# Exercise the original generated package -> CPack -> preinstall Make chain,
# rather than assuming cmake -E env is equivalent to that chain.
set(package_source "${PROBE_DIR}/package-source")
set(package_build "${PROBE_DIR}/package-build")
file(MAKE_DIRECTORY "${package_source}")
file(WRITE "${package_source}/payload.txt" "Windows Make/CPack diagnostic payload\n")
file(WRITE "${package_source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(MakeSyncDiagnostic NONE)
if(NOT EXISTS "${SYNC_PROBE}")
  message(FATAL_ERROR "SYNC_PROBE executable missing")
endif()
# An always-run target makes both the outer build and CPack preinstall exercise
# child output handling, even when the package payload is already up to date.
add_custom_target(sync_handle_observation ALL
  COMMAND "${SYNC_PROBE}" package_all
  VERBATIM)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/payload.txt" DESTINATION share)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_VERSION 1.0.0)
set(CPACK_PACKAGE_FILE_NAME make-sync-diagnostic)
include(CPack)
]=])
run_probe(package_configure "${CMAKE_COMMAND}" -S "${package_source}" -B "${package_build}"
  -G "MinGW Makefiles" "-DCMAKE_MAKE_PROGRAM=${PROBE_MAKE}"
  "-DSYNC_PROBE=${PROBE_DIR}/probe.exe")
get_filename_component(cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
find_program(PROBE_CPACK NAMES cpack HINTS "${cmake_bin}" REQUIRED)
# Record failures/timeouts as experiment outcomes. This diagnostic must not
# claim that completion fixes #507 or that a failed experimental path fails
# the production preinstall/direct-CPack workaround.
function(observe_package label)
  execute_process(COMMAND ${ARGN}
    WORKING_DIRECTORY "${package_build}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
  file(WRITE "${PROBE_DIR}/${label}.log" "${out}${err}\nresult=${result}\n")
  file(APPEND "${PROBE_DIR}/probe.log"
    "\n=== ${label} ===\n${out}${err}\nresult=${result}\n")
  message(STATUS "${label}:\n${out}${err}\nobserved_result=${result}")
  if(NOT "${result}" STREQUAL "0" AND label MATCHES "_control$")
    message(FATAL_ERROR "Packaging control ${label} failed: ${result}")
  endif()
  if(NOT "${result}" STREQUAL "0")
    message(WARNING "Experimental packaging path ${label}: ${result}; retained as diagnostic evidence")
  endif()
endfunction()
# Run the safe production shape first, before any intentionally risky nested
# experiment. An explicit parallel level reproduces CPack's inner -j4 launch.
set(saved_parallel "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 4)
observe_package(package_preinstall_control "${CMAKE_COMMAND}" --build . --target preinstall
  --parallel 4 -- --output-sync=target)
observe_package(package_direct_cpack_control "${PROBE_CPACK}" --config CPackConfig.cmake --verbose)
observe_package(package_outer_make_no_sync "${CMAKE_COMMAND}" --build . --target package
  --parallel 4 -- --output-sync=none)
observe_package(package_outer_make_target_sync "${CMAKE_COMMAND}" --build . --target package
  --parallel 4 -- --output-sync=target)
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "${saved_parallel}")

message(STATUS "Probe completed. Handle validity/type differences are observations, not assertions of the original hang's cause.")
