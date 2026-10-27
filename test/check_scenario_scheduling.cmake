# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.

# Nested fixtures use an OS lock as an independent overlap detector. CTest, not
# this lock, must serialize resource users and exclude the performance fixture.
if(DEFINED PROBE_LOCK)
  file(LOCK "${PROBE_LOCK}" GUARD PROCESS TIMEOUT 0 RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "CTest ran mutually exclusive fixtures concurrently: ${lock_result}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.3 RESULT_VARIABLE sleep_result)
  if(NOT sleep_result EQUAL 0)
    message(FATAL_ERROR "Scheduling probe sleep failed")
  endif()
  return()
endif()

foreach(required CTEST RUNNER BUILD DATA GENERATOR MAKE_PROGRAM)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${required}")
  endif()
endforeach()
string(RANDOM LENGTH 20 ALPHABET 0123456789abcdef token)
set(scratch "${BUILD}/scenario-scheduling-${token}")
file(MAKE_DIRECTORY "${scratch}/source" "${scratch}/work")
set(helper "${CMAKE_CURRENT_LIST_DIR}/ScenarioRegistration.cmake")
set(probe "${CMAKE_CURRENT_LIST_FILE}")
set(project [==[
cmake_minimum_required(VERSION 3.13)
project(ScenarioScheduling NONE)
enable_testing()
include([=[@helper@]=])
goat_scenario(acceptance.first continuation TIMEOUT 20 COMMAND
  [=[@RUNNER@]=] fixture-fail [=[@scratch@/work]=] [=[@DATA@]=])
goat_scenario(acceptance.after continuation TIMEOUT 20 COMMAND
  [=[@RUNNER@]=] fixture-pass [=[@scratch@/work]=] [=[@DATA@]=])
set_tests_properties(acceptance.after PROPERTIES DEPENDS acceptance.first)
goat_scenario(acceptance.skip continuation TIMEOUT 20 COMMAND
  [=[@RUNNER@]=] explicit-skip [=[@scratch@/work]=] [=[@DATA@]=])
foreach(id independent_a independent_b)
  goat_scenario(acceptance.${id} scheduling TIMEOUT 20 COMMAND
    [=[@RUNNER@]=] fixture-pass [=[@scratch@/work]=] [=[@DATA@]=])
  get_test_property(acceptance.${id} RUN_SERIAL serial)
  if(serial)
    message(FATAL_ERROR "Independent scenarios must allow parallel execution")
  endif()
endforeach()
foreach(id locked_a locked_b)
  goat_scenario(acceptance.${id} scheduling TIMEOUT 20 LOCKS acceptance_shared COMMAND
    "${CMAKE_COMMAND}" [=[-DPROBE_LOCK=@scratch@/shared.lock]=] -P [=[@probe@]=])
  get_test_property(acceptance.${id} RESOURCE_LOCK locks)
  if(NOT locks STREQUAL "acceptance_shared")
    message(FATAL_ERROR "Shared resource registration was lost")
  endif()
endforeach()
goat_scenario(acceptance.performance scheduling TIMEOUT 20 PERFORMANCE COMMAND
  "${CMAKE_COMMAND}" [=[-DPROBE_LOCK=@scratch@/shared.lock]=] -P [=[@probe@]=])
get_test_property(acceptance.performance RUN_SERIAL serial)
if(NOT serial)
  message(FATAL_ERROR "Performance must exclude concurrent tests")
endif()
]==])
string(CONFIGURE "${project}" project @ONLY)
file(WRITE "${scratch}/source/CMakeLists.txt" "${project}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${scratch}/source" -B "${scratch}/build"
    -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
  RESULT_VARIABLE configured OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 30)
if(NOT configured EQUAL 0)
  message(FATAL_ERROR "Nested configure failed; artifacts: ${scratch}\n${output}\n${errors}")
endif()

# Deliberately do not use WILL_FAIL or --stop-on-failure: the first test really
# fails, yet CTest must still execute the next independently registered scenario.
execute_process(COMMAND "${CTEST}" --test-dir "${scratch}/build" --parallel 1
    --output-on-failure -V -L continuation
  RESULT_VARIABLE failed OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT failed EQUAL 8 OR
   NOT output MATCHES "acceptance.first[ .]+\\*\\*\\*Failed" OR
   NOT output MATCHES "acceptance.after[ .]+Passed" OR
   NOT output MATCHES "acceptance.skip[ .]+\\*\\*\\*Skipped" OR
   NOT output MATCHES "intentional skip fixture: optional capability unavailable")
  message(FATAL_ERROR "Failure continuation/skip contract failed; artifacts: ${scratch}\n${output}\n${errors}")
endif()
# Prove that the expected failure reached the scenario body rather than failing
# to launch, select an ID, or initialize its context.
file(GLOB failure_logs "${scratch}/work/scenario-*/execution.log")
list(LENGTH failure_logs failure_log_count)
if(NOT failure_log_count EQUAL 1)
  message(FATAL_ERROR "Expected one retained failure log; artifacts: ${scratch}")
endif()
list(GET failure_logs 0 failure_log)
file(READ "${failure_log}" failure_details)
if(NOT failure_details MATCHES "FAIL: intentional fixture failure")
  message(FATAL_ERROR "Wrong intentional failure; artifacts: ${scratch}\n${failure_details}")
endif()
execute_process(COMMAND "${CTEST}" --test-dir "${scratch}/build" --parallel 4
    --output-on-failure -L scheduling
  RESULT_VARIABLE parallel OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT parallel EQUAL 0 OR NOT output MATCHES "100% tests passed")
  message(FATAL_ERROR "Parallel scheduling contract failed; artifacts: ${scratch}\n${output}\n${errors}")
endif()
file(REMOVE_RECURSE "${scratch}")
message(STATUS "Verified failure continuation, explicit skip, parallel scenarios, resource locks and exclusive performance")
