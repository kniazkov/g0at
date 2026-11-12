cmake_minimum_required(VERSION 3.20)
if(NOT WIN32)
  message(FATAL_ERROR "Native Windows CMake required")
endif()
unset(ENV{MAKEFLAGS})
unset(ENV{MFLAGS})
unset(ENV{GNUMAKEFLAGS})
find_program(CC NAMES gcc cc REQUIRED)
find_program(MAKE NAMES mingw32-make REQUIRED)
set(work "${CMAKE_CURRENT_BINARY_DIR}/build/ci/sync-reuse")
file(MAKE_DIRECTORY "${work}")
execute_process(COMMAND "${CC}" "${CMAKE_CURRENT_LIST_DIR}/relay.c" -O0 -g -Wall -Wextra
  -o "${work}/relay.exe" RESULT_VARIABLE rc TIMEOUT 30)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Compile failed: ${rc}")
endif()
file(WRITE "${work}/tiny.mk" ".PHONY: child\nchild:\n\t@echo synthetic-child-output\n")
file(WRITE "${work}/outer.mk" [=[
.PHONY: run
run:
	"$(CMAKE)" -DCASE=$(CASE) "-DMAKE=$(NATIVE_MAKE)" -P child.cmake
]=])
file(WRITE "${work}/child.cmake" [=[
execute_process(COMMAND "${CMAKE_CURRENT_LIST_DIR}/relay.exe" "${CASE}" "${MAKE}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" RESULT_VARIABLE rc TIMEOUT 20)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Synthetic ${CASE} did not confirm predicted result: ${rc}")
endif()
]=])
foreach(mode stale stripped)
  execute_process(COMMAND "${MAKE}" -j4 --output-sync=target -f outer.mk run
    "CMAKE=${CMAKE_COMMAND}" "NATIVE_MAKE=${MAKE}" "CASE=${mode}"
    WORKING_DIRECTORY "${work}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
    TIMEOUT 25)
  file(WRITE "${work}/${mode}.log" "${out}${err}\nresult=${rc}\n")
  message(STATUS "SYNTHETIC ${mode}:\n${out}${err}\nresult=${rc}")
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "Synthetic ${mode} failed/inconclusive; see ${work}/${mode}.log")
  endif()
endforeach()
message(STATUS "Synthetic pair confirmed; this does NOT prove cause of the historical CPack stall.")
