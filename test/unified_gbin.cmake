# Exercise the public artifact contract with actual tools, without a source compiler on reload.
foreach(name GOAT INSPECT MERGE WORK)
  if(NOT DEFINED ${name})
    message(FATAL_ERROR "Missing ${name}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
function(run expected)
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT status EQUAL 0 OR NOT out STREQUAL "${expected}")
    message(FATAL_ERROR "${ARGN}: status=${status}\nstdout=${out}\nstderr=${err}")
  endif()
endfunction()
file(WRITE "${WORK}/sample.goat" "external const answer = func() { return 42; }; println(answer());\n")
run("42\n" "${GOAT}" "${WORK}/sample.goat" --native off)
run("" "${GOAT}" "${WORK}/sample.goat" --compile --native off)
file(READ "${WORK}/sample.gbin" magic LIMIT 4 HEX)
if(NOT magic STREQUAL "504b0304")
  message(FATAL_ERROR "--compile did not produce ZIP")
endif()
execute_process(COMMAND "${INSPECT}" "${WORK}/sample.gbin" RESULT_VARIABLE status
  OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "goat-inspect rejected --compile output: ${report}\n${err}")
endif()
file(MAKE_DIRECTORY "${WORK}/parts")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${WORK}/sample.gbin"
  WORKING_DIRECTORY "${WORK}/parts" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Cannot inspect ZIP members")
endif()
file(READ "${WORK}/parts/module.json" metadata)
string(JSON name GET "${metadata}" name)
string(JSON version GET "${metadata}" version)
if(NOT name STREQUAL "sample" OR NOT version STREQUAL "0")
  message(FATAL_ERROR "Unexpected synthetic identity: ${name}@${version}")
endif()
execute_process(COMMAND "${MERGE}" "${WORK}/merged.gbin" "${WORK}/sample.gbin"
  RESULT_VARIABLE status OUTPUT_VARIABLE report ERROR_VARIABLE err)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "goat-merge rejected --compile output: ${report}\n${err}")
endif()
run("42\n" "${GOAT}" "${WORK}/merged.gbin" --native off)
file(MAKE_DIRECTORY "${WORK}/modules/sample")
file(COPY_FILE "${WORK}/sample.gbin" "${WORK}/modules/sample/0.gbin")
file(WRITE "${WORK}/consumer.goat" "var m = import \"sample@0\"; println(m.answer());\n")
run("42\n42\n" "${GOAT}" "${WORK}/consumer.goat" --module-dir "${WORK}/modules" --native off)
# Source deletion proves that loading cannot silently recompile the original.
file(REMOVE "${WORK}/sample.goat")
run("42\n" "${GOAT}" "${WORK}/sample.gbin" --native off --cc missing-compiler-316)
file(WRITE "${WORK}/Bad Name.goat" "println(42);\n")
execute_process(COMMAND "${GOAT}" "${WORK}/Bad Name.goat" --compile --native off
  RESULT_VARIABLE status ERROR_VARIABLE err OUTPUT_VARIABLE out)
if(status EQUAL 0 OR NOT err MATCHES "rename it or use an explicit .gmod")
  message(FATAL_ERROR "Invalid synthetic name was not diagnosed: ${out}${err}")
endif()
file(WRITE "${WORK}/legacy.gbin" "GOATPG01obsolete")
foreach(input legacy.gbin parts/bytecode.bin)
  execute_process(COMMAND "${GOAT}" --run "${WORK}/${input}" --native off
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(status EQUAL 0)
    message(FATAL_ERROR "Accepted non-ZIP artifact ${input}")
  endif()
endforeach()
message(STATUS "Unified ZIP compilation/loading contract passed")
# Warnings from the shared compiler must survive source observer/export lifetime.
file(WRITE "${WORK}/warning.goat" "switch(1) { case 1: print(1); case 1: print(2); }\n")
execute_process(COMMAND "${GOAT}" --lang en --enable-warnings --compile "${WORK}/warning.goat"
  RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT err MATCHES "[Ww]arning" OR NOT out STREQUAL "")
  message(FATAL_ERROR "Compiler warning was lost: ${status} ${out} ${err}")
endif()
# Continuation-only native libraries preserve the existing descriptor/schema and
# execute immutable bytecode identities after both source and compiler disappear.
file(WRITE "${WORK}/objects.goat" [=[
var bump = func(obj) {
    obj["value"] = obj["value"] + 1;
    return obj["value"];
};
var box = { var value = 41 };
println(bump(box));
println(box.value);
]=])
run("42\n42\n" "${GOAT}" "${WORK}/objects.goat" --native off)
run("" "${GOAT}" "${WORK}/objects.goat" --compile --native required)
file(MAKE_DIRECTORY "${WORK}/objects-parts")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${WORK}/objects.gbin"
  WORKING_DIRECTORY "${WORK}/objects-parts" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Cannot inspect continuation ZIP members")
endif()
file(READ "${WORK}/objects-parts/module.json" metadata)
string(JSON abi GET "${metadata}" native 0 goatAbiVersion)
string(JSON scalar_count LENGTH "${metadata}" native 0 specializations)
if(NOT abi EQUAL 1 OR NOT scalar_count EQUAL 0)
  message(FATAL_ERROR "Continuation-only module fabricated scalar ABI specializations")
endif()
file(REMOVE "${WORK}/objects.goat")
run("42\n42\n" "${GOAT}" --run "${WORK}/objects.gbin" --native required
    --cc missing-compiler-503 --save-native "${WORK}/objects.report")
file(READ "${WORK}/objects.report" native_report)
if(NOT native_report MATCHES "bound=[1-9][0-9]*"
   OR NOT native_report MATCHES "attempts=[1-9][0-9]*"
   OR NOT native_report MATCHES "succeeded=[1-9][0-9]*")
  message(FATAL_ERROR "Persisted object call did not execute native continuation: ${native_report}")
endif()
run("42\n42\n" "${GOAT}" --run "${WORK}/objects.gbin" --native off
    --cc missing-compiler-503)
