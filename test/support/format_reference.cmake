# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Preserve format.reference without Python. See test/data/format-reference/README.md.
# Stream complete child output; the scenario runners retain all failure artifacts.
function(reference_check)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result TIMEOUT 100)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Reference coverage failed (${result}): ${ARGV}")
  endif()
endfunction()
foreach(scenario check roundtrip reject-lossy-source)
  reference_check("${SCHEMA_TEST}" "${scenario}" "${GENERATOR}" "${ROOT}" "${WORK}")
endforeach()
reference_check("${ORACLE}" all "${WORK}" "${ROOT}/test/data"
  "${ROOT}/docs/module-format-v1/schema-source.json")
reference_check("${BINARY}" all "${WORK}" "${ROOT}/test/data"
  "${ROOT}/docs/module-format-v1/schema-source.json")
