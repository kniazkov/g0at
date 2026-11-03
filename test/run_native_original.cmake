# Copyright 2026 Ivan Kniazkov
# Temporary unchanged-Python parity gate for #384, removed only after full CI.
if(NOT CHECK MATCHES "^(access|libm)$" OR NOT EXISTS "${PYTHON}")
  message(FATAL_ERROR "Original parity requires an explicit Python interpreter and check")
endif()
string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef nonce)
set(work "${BUILD}/native-original-${CHECK}/scenario-${nonce}")
file(MAKE_DIRECTORY "${work}")
execute_process(COMMAND "${PYTHON}" "${REPOSITORY}/test/src/native/checks/${CHECK}.py" "${GOAT}" "${work}"
  WORKING_DIRECTORY "${REPOSITORY}" TIMEOUT 300 RESULT_VARIABLE status
  OUTPUT_FILE "${work}/original.stdout" ERROR_FILE "${work}/original.stderr")
file(READ "${work}/original.stdout" output)
file(READ "${work}/original.stderr" error)
message(STATUS "Original ${CHECK}: ${output}")
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "Original ${CHECK} failed (${status}); ${work}\n${error}")
endif()
# Match Path.write_text's platform newline expansion, without changing frozen inputs.
function(compare_fixture original fixture)
  file(READ "${work}/${original}" actual)
  file(READ "${REPOSITORY}/test/data/native-${CHECK}/${fixture}" expected)
  if(WIN32)
    string(REPLACE "\n" "\r\n" expected "${expected}")
  endif()
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Original bytes differ: ${original} / ${fixture}; ${work}")
  endif()
endfunction()
if(CHECK STREQUAL "access")
  compare_fixture(blocked.goat blocked.goat)
  compare_fixture(retry.goat retry.goat)
else()
  file(GLOB fixtures RELATIVE "${REPOSITORY}/test/data/native-libm"
    "${REPOSITORY}/test/data/native-libm/unary-*.goat"
    "${REPOSITORY}/test/data/native-libm/binary-*.goat"
    "${REPOSITORY}/test/data/native-libm/semantics-*.goat"
    "${REPOSITORY}/test/data/native-libm/reject-*.goat")
  foreach(fixture IN LISTS fixtures)
    compare_fixture("${fixture}" "${fixture}")
  endforeach()
  foreach(name IN ITEMS cos sqrt pow fmod min max atan abs)
    compare_fixture("host-${name}.c" "host-${name}.c.txt")
  endforeach()
  compare_fixture(zero-contract.goat zero-max.goat)
  compare_fixture(edges.goat edges-abs.goat)
endif()
message(STATUS "Original ${CHECK} passed, generated fixture bytes match; retained ${work}")
