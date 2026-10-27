#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Compare every native string operation in a DLL/SO against unoptimized VM execution."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]


def literal(text):
    return json.dumps(text, ensure_ascii=False).replace("\\u0000", "\\0")


def goat_literal(text):
    return "(" + "+chr(0)+".join(json.dumps(part, ensure_ascii=False) for part in text.split("\0")) + ")"


def main():
    interpreter = Path(sys.argv[1]).resolve()
    work = Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("CC", "gcc")
    flags = sys.argv[3:]
    c = []
    vm = ['const show = func(s) { print(s.length); print(":"); '
          'for(var i=0;i<s.length;i=i+1){print(ord(s.charAt(i)));print(",");} println(""); };']
    cases = 0

    def case(op, value, *args):
        nonlocal cases
        cases += 1
        c_args = ["V(U" + literal(value) + ")"]
        c_args += ["V(U" + literal(a) + ")" if isinstance(a, str) else str(a) for a in args]
        names = {"char_at": "charAt", "index_of": "indexOf", "last_index_of": "lastIndexOf",
                 "starts_with": "startsWith", "ends_with": "endsWith", "is_empty": "isEmpty",
                 "is_blank": "isBlank", "to_upper_case": "toUpperCase", "to_lower_case": "toLowerCase"}
        method = names.get(op, op)
        expression = goat_literal(value) + "." + method + "(" + ",".join(
            goat_literal(a) if isinstance(a, str) else str(a) for a in args) + ")"
        string_result = op in ("trim", "substring", "to_upper_case", "to_lower_case",
                               "replace", "repeat", "format", "concat")
        joined = ", ".join(c_args)
        if op == "concat":
            expression = goat_literal(value) + "+" + goat_literal(args[0])
        if op == "format":
            array = ",".join(c_args[1:])
            joined = c_args[0] + ", " + ("(goat_native_string_view_v2_t[]){" + array + "}" if args else "NULL")
            joined += ", " + str(len(args))
        if string_result:
            c.append("{ goat_native_string_buffer_v2_t out = {0}; "
                     f"assert(goat_native_string_{op}(context, {joined}, &out) == GOAT_NATIVE_OK); "
                     "show(out); goat_native_string_release(context, &out); }")
            vm.append("show(" + expression + ");")
        elif op == "char_at":
            c.append("{ uint32_t out = 0; " + f"assert(goat_native_string_char_at({joined}, &out) == GOAT_NATIVE_OK); "
                     'printf("%" PRIu32 "\\n", out); }')
            vm.append("println(ord(" + expression + "));")
        elif op == "is_blank":
            c.append("{ bool out = false; " + f"assert(goat_native_string_is_blank(context, {joined}, &out) == GOAT_NATIVE_OK); "
                     'puts(out ? "true" : "false"); }')
            vm.append("println(" + expression + ");")
        elif op in ("length", "index_of", "last_index_of"):
            c.append('printf("%" PRId64 "\\n", ' + f"goat_native_string_{op}({joined}));")
            vm.append("println(" + (goat_literal(value) + ".length" if op == "length" else expression) + ");")
        else:
            c.append(f'puts(goat_native_string_{op}({joined}) ? "true" : "false");')
            vm.append("println(" + expression + ");")

    for text in ("", "abc", " \t\n", "  Привет 😀  ", "a\0b", "ÄÖé Яя", "\u2003\u00a0"):
        for op in ("length", "is_empty", "is_blank", "trim", "to_upper_case", "to_lower_case"):
            case(op, text)
        for needle in ("", "a", "b", "нет", text):
            for op in ("contains", "index_of", "last_index_of", "starts_with", "ends_with"):
                case(op, text, needle)
        for count in (0, 1, 3):
            case("repeat", text, count)
        case("concat", text, "尾\0😀")
        case("substring", text, 0, 0)
    for op in ("index_of", "last_index_of", "contains"):
        case(op, "ababa", "aba")
    case("char_at", "abc", 2)
    case("char_at", "a\0b", 1)
    case("char_at", "😀", 0)
    case("substring", "abcde", 1, 4)
    for value, old, new in (("aaaaa", "aa", "b"), ("ababa", "aba", "xyz"),
                            ("abc", "", "x"), ("abc", "longer", "x"),
                            ("abcabc", "abc", ""), ("a\0a", "\0", "XY")):
        case("replace", value, old, new)
    for text in ("%0-%1-%0", "%%0 %01 %10 %2 %", "%9999999999999999999999999999999999999",
                 "x\0%0", "%0000000000000000000000000000000000000"):
        case("format", text, "Я\0", "😀")
    case("format", "%0 %1")
    case("format", "%10/%0011/%0", *[str(i) for i in range(12)])
    for suffix, native_arg, vm_arg in [
        ("i64", "INT64_MAX", "9223372036854775807"),
        ("i64", "-INT64_MAX", "-9223372036854775807"),
        ("f64", "1.25", "1.25"), ("f64", "-0.0", "-0.0"),
        ("f64", "1e-11", "0.00000000001"),
        ("bool", "true", "true"), ("bool", "false", "false"),
        ("char", "0", "chr(0)"), ("char", "128512", "chr(128512)")]:
        cases += 1
        c.append("{ goat_native_string_buffer_v2_t out = {0}; "
                 f"assert(goat_native_str_{suffix}(context, {native_arg}, &out) == GOAT_NATIVE_OK); "
                 "show(out); goat_native_string_release(context, &out); }")
        vm.append(f"show(str({vm_arg}));")
    module = work / "strings.c"
    module.write_text('''#include "goat/native_strings.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#define V(s) ((goat_native_string_view_v2_t){s, sizeof(s)/sizeof(gchar_t)-1})
static void show(goat_native_string_buffer_v2_t value) {
    printf("%zu:", value.length);
    for(size_t i=0;i<value.length;++i) printf("%" PRIu32 ",", (uint32_t)value.data[i]);
    puts("");
    assert(value.length <= value.capacity);
    assert(!value.data || value.data[value.length] == 0);
}
#include "checks.h"
GOAT_NATIVE_API void GOAT_NATIVE_CALL run_strings(const goat_native_context_v2_t *context) {
''' + "\n".join(c) + '\nboundaries(context);\n}\n', encoding="utf-8")
    program = work / "strings.goat"
    program.write_text("\n".join(vm), encoding="utf-8")
    library = work / ("strings.dll" if os.name == "nt" else "strings.so")
    host = work / "host.exe"
    common = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
              "-I" + str(ROOT / "include"), *flags]
    shared = ["-shared", "-DGOAT_NATIVE_BUILD"]
    shared += ["-static", "-static-libgcc"] if os.name == "nt" else ["-fPIC"]
    # Use the exact archive produced by the interpreter build. Sanitizer runs rebuild
    # the library too, so the algorithms (not just the callers) are instrumented.
    runtime = interpreter.parent / "libgoat_native.a"
    assert runtime.is_file(), runtime
    if flags:
        runtime = work / "libgoat_native-instrumented.a"
        objects = []
        for name in ("src/native/strings.c", "src/native/str.c", "lib/numbers/number_format.c", "lib/strings/unicode.c"):
            obj = work / (Path(name).stem + "-runtime.o")
            subprocess.run([*common, "-I" + str(ROOT), "-I" + str(ROOT / "src"), "-fPIC", "-c", str(ROOT / name),
                            "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run([os.environ.get("AR", "ar"), "rcs", str(runtime), *objects], check=True)
    subprocess.run([*common, *shared, "-I" + str(ROOT / "test/src/native/runtime_strings"),
                    str(module), str(runtime), "-o", str(library)], check=True)
    subprocess.run([*common, "-DMEMORY_DEBUG", "-I" + str(ROOT), "-I" + str(ROOT / "src"),
                    str(ROOT / "test/src/native/runtime_strings/host.c"),
                    str(ROOT / "src/model/native_services.c"), str(ROOT / "lib/strings/unicode.c"), str(ROOT / "lib/memory/allocate.c"),
                    "-o", str(host), *([] if os.name == "nt" else ["-ldl"])], check=True)
    native = subprocess.run([str(host), str(library)], check=True, capture_output=True, timeout=90)
    reference = subprocess.run([str(interpreter), "--optimize", "none", "--native", "off", str(program)],
                               check=True, capture_output=True, timeout=90)
    (work / "native.txt").write_bytes(native.stdout)
    (work / "vm.txt").write_bytes(reference.stdout)
    assert native.stdout.splitlines() == reference.stdout.splitlines(), "Native/VM mismatch; see native.txt and vm.txt"
    if not flags:
        # Exercise the interpreter's real compiler backend, which stages and links
        # its embedded archive rather than the build-directory copy above.
        automatic_source = work / "automatic.c"
        checks = (ROOT / "test/src/native/runtime_strings/checks.h").read_text(encoding="utf-8")
        automatic_source.write_text(module.read_text(encoding="utf-8")
                                    .replace('"goat/native_strings.h"', '"native_strings.h"')
                                    .replace('#include "checks.h"', checks.replace('#pragma once', '')),
                                    encoding="utf-8")
        automatic_host = work / "compiler-host.exe"
        subprocess.run([*common, "-DTEST_NATIVE_COMPILER", "-I" + str(ROOT), "-I" + str(ROOT / "src"),
                        str(ROOT / "test/src/native/runtime_strings/host.c"),
                        str(interpreter.parent / "libcore.a"), str(interpreter.parent / "libcommon.a"), "-o", str(automatic_host),
                        "-lpthread", "-lm", *( ["-lbcrypt"] if os.name == "nt" else ["-ldl"])],
                       check=True)
        automatic_library = work / ("automatic.dll" if os.name == "nt" else "automatic.so")
        automatic = subprocess.run([str(automatic_host), str(automatic_library), cc,
                                    str(automatic_source)], capture_output=True, timeout=90)
        assert automatic.returncode == 0, automatic.stderr
        assert automatic.stdout.splitlines() == reference.stdout.splitlines(), automatic.stdout
        assert not list(work.glob("automatic.*.goat-*")), "Temporary SDK files were not removed"
    invalid = ['"x".charAt(-1)', '"x".charAt(9223372036854775807)',
               '"abc".substring(-1,2)', '"abc".substring(2,1)',
               '"abc".substring(0,9223372036854775807)',
               '"abc".repeat(-1)', '"abc".repeat(9223372036854775807)']
    for index, expression in enumerate(invalid):
        bad = work / f"invalid-{index}.goat"
        bad.write_text("println(" + expression + ");", encoding="utf-8")
        result = subprocess.run([str(interpreter), "--lang", "en", "--optimize", "none",
                                 "--native", "off", str(bad)], capture_output=True, timeout=90)
        assert result.returncode != 0 and b"INVALID_ARGUMENT" in result.stderr, result
    print(f"Native strings: {cases} VM comparisons and boundary/ownership checks passed")


if __name__ == "__main__":
    main()
