# Copyright 2026 Ivan Kniazkov
# Embed public headers as byte arrays; invoked by the build, not edited by hand.
if(NOT DEFINED INPUT_DIR OR NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "INPUT_DIR and OUTPUT_FILE are required")
endif()

set(output "/* Generated from public native headers; do not edit. */\n")
foreach(name native_abi native_runtime native_strings)
  # HEX preserves source bytes and avoids locale/encoding dependencies.
  file(READ "${INPUT_DIR}/${name}.h" data HEX)
  string(APPEND output "static const unsigned char ${name}_data[] = {\n")
  string(LENGTH "${data}" length)
  set(offset 0)
  while(offset LESS length)
    math(EXPR count "${length} - ${offset}")
    if(count GREATER 32)
      set(count 32)
    endif()
    string(SUBSTRING "${data}" ${offset} ${count} row)
    string(REGEX REPLACE "(..)" "0x\\1, " row "${row}")
    string(APPEND output "    ${row}\n")
    math(EXPR offset "${offset} + ${count}")
  endwhile()
  string(APPEND output "};\n")
endforeach()

get_filename_component(output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT_FILE}" "${output}")
