# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_HOST_WIN32)
  message(FATAL_ERROR "This control requires a native Windows CMake")
endif()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(OUTPUT_DIR "${OUTPUT_DIR}" ABSOLUTE)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/build_command.cmake")
file(WRITE "${OUTPUT_DIR}/results.tsv" "iteration\tphase\tstart_utc\tseconds\tresult\n")
function(run_phase phase)
  string(TIMESTAMP started "%Y-%m-%dT%H:%M:%SZ" UTC)
  string(TIMESTAMP before "%s" UTC)
  set(log "${OUTPUT_DIR}/${iteration}-${phase}.log")
  file(APPEND "${OUTPUT_DIR}/events.txt" "${started} START ${iteration} ${phase}: ${ARGN}\n")
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${root}"
    RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}" TIMEOUT 600)
  string(TIMESTAMP after "%s" UTC)
  math(EXPR elapsed "${after} - ${before}")
  file(APPEND "${OUTPUT_DIR}/results.tsv" "${iteration}\t${phase}\t${started}\t${elapsed}\t${result}\n")
  message(STATUS "Clean package ${iteration}/2 ${phase}: ${result}, ${elapsed}s")
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Clean package ${iteration} ${phase} failed: ${result}; see ${log}")
  endif()
endfunction()
foreach(iteration RANGE 1 2)
  set(directory "${root}/build/package-clean-${iteration}")
  # Never erase an existing build or mistake a warm build for a clean one.
  if(EXISTS "${directory}")
    message(FATAL_ERROR "Clean build requires an absent directory: ${directory}")
  endif()
  run_phase(configure "${CMAKE_COMMAND}" -S "${root}" -B "${directory}"
    -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON)
  goat_build_command(command "${directory}" --parallel 4 --target preinstall --config Release)
  run_phase(preinstall ${command})
  run_phase(cpack "${CMAKE_COMMAND}" -E chdir "${directory}"
    "${cmake_bin}/cpack.exe" --config CPackConfig.cmake -C Release)
  # Register the relocated-package check now that the archive exists.
  run_phase(register "${CMAKE_COMMAND}" -S "${root}" -B "${directory}")
  run_phase(relocated "${cmake_bin}/ctest.exe" --test-dir "${directory}"
    --output-on-failure --no-tests=error -R "^package[.]relocated$")
endforeach()
