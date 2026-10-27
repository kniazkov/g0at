# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(work "${WORK}-${suffix}")
file(MAKE_DIRECTORY "${work}")
file(COPY "${SOURCE}/" DESTINATION "${work}")
execute_process(COMMAND "${RUNNER}" "${GOAT}" list.txt WORKING_DIRECTORY "${work}" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Functional tests failed (${status}); diagnostics retained in ${work}")
endif()
file(REMOVE_RECURSE "${work}")
