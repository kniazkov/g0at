# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# One independently selectable scenario per CTest process. Legacy goat_suite
# registrations remain serial until their shared-resource contracts are migrated.
function(goat_scenario name labels)
  cmake_parse_arguments(SC "PERFORMANCE" "TIMEOUT" "LOCKS;COMMAND" ${ARGN})
  if(NOT SC_COMMAND OR NOT SC_TIMEOUT MATCHES "^[1-9][0-9]*$" OR SC_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Invalid scenario registration: ${name}")
  endif()
  add_test(NAME ${name} COMMAND ${SC_COMMAND})
  set_tests_properties(${name} PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS "${labels}"
    ENVIRONMENT "CC=${CMAKE_C_COMPILER};PYTHONDONTWRITEBYTECODE=1"
    RUN_SERIAL FALSE TIMEOUT "${SC_TIMEOUT}"
    SKIP_RETURN_CODE 77)
  if(SC_LOCKS)
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK "${SC_LOCKS}")
  endif()
  if(SC_PERFORMANCE)
    set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE LABELS "${labels};performance")
  endif()
endfunction()
