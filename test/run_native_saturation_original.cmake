# Copyright 2026 Ivan Kniazkov
# Temporary #386 original/C parity gate. Retire only after complete green CI.
if(NOT EXISTS "${PYTHON}")
  message(FATAL_ERROR "Original saturation parity needs an explicit Python")
endif()
string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef nonce)
set(work "${BUILD}/native-original-saturation/scenario-${nonce}")
set(original "${work}/original")
set(converted "${work}/converted")
file(MAKE_DIRECTORY "${original}" "${converted}")
# The unchanged Python preserves its 120-second commands and 30-second invalid
# calls. The C runner retains the old 300-second internal aggregate cap;
# its outer process allowance adds 30 seconds only for supervisor cleanup.
execute_process(COMMAND "${PYTHON}" "${REPOSITORY}/test/src/native/checks/saturation.py"
  "${GOAT}" "${original}"
  WORKING_DIRECTORY "${REPOSITORY}" TIMEOUT 300 RESULT_VARIABLE original_status
  OUTPUT_FILE "${work}/original.stdout" ERROR_FILE "${work}/original.stderr")
if(NOT original_status STREQUAL "0")
  file(READ "${work}/original.stdout" output)
  file(READ "${work}/original.stderr" error)
  message(FATAL_ERROR "Original saturation failed (${original_status}); ${work}\n${output}\n${error}")
endif()
execute_process(COMMAND "${RUNNER}" all "${GOAT}" "${REPOSITORY}" "${converted}"
  "${REPOSITORY}/test/data" --keep-work
  WORKING_DIRECTORY "${REPOSITORY}" TIMEOUT 330 RESULT_VARIABLE converted_status
  OUTPUT_FILE "${work}/converted.stdout" ERROR_FILE "${work}/converted.stderr")
if(NOT converted_status STREQUAL "0")
  file(READ "${work}/converted.stdout" output)
  file(READ "${work}/converted.stderr" error)
  message(FATAL_ERROR "C saturation failed (${converted_status}); ${work}\n${output}\n${error}")
endif()
file(GLOB converted_work LIST_DIRECTORIES true "${converted}/scenario-*")
list(LENGTH converted_work converted_count)
if(NOT converted_count EQUAL 1)
  message(FATAL_ERROR "Expected one C aggregate workspace; found ${converted_count}: ${work}")
endif()
list(GET converted_work 0 converted)
# Text-mode READ normalizes CRLF even on Linux. HEX checks the actual bytes,
# including the Python platform newline convention and missing final newlines.
function(equal_files first second)
  file(READ "${first}" a HEX)
  file(READ "${second}" b HEX)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "Original/C bytes differ: ${first} / ${second}; ${work}")
  endif()
endfunction()
foreach(name IN ITEMS saturation.goat driver.c minimum.goat spaced_minimum.goat double_negation.goat division.goat order.goat unchanged.goat invalid0.goat invalid1.goat invalid2.goat invalid3.goat invalid4.goat invalid5.goat invalid6.goat invalid7.goat)
  equal_files("${original}/${name}" "${converted}/${name}")
endforeach()
# Python checks exact independently computed results; the C oracle must also
# agree byte-for-byte with the LF-only frozen output on every platform.
file(READ "${converted}/expected.txt" actual HEX)
file(READ "${REPOSITORY}/test/data/native-saturation/expected.txt" expected HEX)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "C oracle/frozen expected output differs; ${work}")
endif()
# Each implementation validates its native-report contract. Attempt counts may
# legitimately differ, so reports are deliberately not compared byte-for-byte.
message(STATUS "Original/C saturation parity and frozen expected output passed; retained ${work}")
