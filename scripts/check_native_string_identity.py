#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Exercise generated string adapters and both VM/native marshaling directions."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    interpreter = Path(sys.argv[1]).resolve()
    work = Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / "identity.goat"
    shutil.copyfile(ROOT / "example/native_string_identity.goat", source)

    def run(*args, env=None):
        result = subprocess.run([str(interpreter), "--lang", "en", *map(str, args)],
                                capture_output=True, env=env, timeout=90)
        assert result.returncode == 0, (args, result.stderr)
        assert not result.stderr, result.stderr
        return result.stdout.replace(b"\r\n", b"\n")

    def required(program, calls, persisted=False, env=None, bound=1):
        report = work / "identity.report"
        actual = run(*(["--run"] if persisted else []), "--native", "required",
                     "--save-native", report, program, env=env)
        fields = dict(line.split("=", 1) for line in report.read_text().splitlines())
        assert fields["preparation"] == "ready", fields
        assert fields["mode"] == "required" and fields["bound"] == str(bound), fields
        assert fields["attempts"] == str(calls) and fields["succeeded"] == str(calls), fields
        assert fields["retries"] == "0", fields
        return actual

    expected = "Привет, native strings! 😀\n".encode()
    assert run("--native", "off", source) == expected
    assert required(source, 1) == expected
    generated = run("--print-c", source).decode()
    assert "GOAT_NATIVE_STRING" in generated and "GOAT_NATIVE_PURE" in generated
    assert "goat_native_string_view_v2_t g_f1_s_" in generated
    assert "return g_p0;" in generated

    # Link a host against the real generated adapter and check boundary validation.
    run("--save-c", source)
    driver = work / "adapter.exe"
    compiler = os.environ.get("CC", "gcc")
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DMEMORY_DEBUG", "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                    str(ROOT / "test/functional/native_string_identity/driver.c"),
                    str(source.with_suffix(".c")), str(ROOT / "src/model/native_services.c"),
                    str(ROOT / "src/lib/allocate.c"), str(work / "libgoat_native.a"),
                    "-lm", "-o", str(driver)], check=True, timeout=90)
    subprocess.run([str(driver)], check=True, timeout=30)

    # Dynamic inputs, empty strings and embedded NULs; then overwrite all input references.
    # A result must survive both context cleanup and release of its original VM argument.
    cases = work / "lifetime.goat"
    cases.write_text('''const identity = func(value) { return value; };
var input = "left" + chr(0) + "right😀";
var result = identity(input);
input = "replaced";
println(result == "left" + chr(0) + "right😀");
println(ord(result.charAt(4)));
println(identity("") == "");
println(identity("temporary" + chr(0) + "tail") == "temporary" + chr(0) + "tail");
for (var i = 0; i < 100; i = i + 1) {
    var temporary = identity("prefix" + chr(0) + "suffix");
    if (temporary != "prefix" + chr(0) + "suffix") throw "mismatch";
}
println(result == "left" + chr(0) + "right😀");
''', encoding="utf-8")
    reference = run("--native", "off", cases)
    assert reference == b"true\n0\ntrue\ntrue\ntrue\n", reference
    assert required(cases, 103) == reference
    # A saved module must preserve its string signature and work without source/compiler.
    assert run("--compile", "--native", "required", cases) == b""
    cases.unlink()
    environment = os.environ.copy()
    environment["CC"] = str(work / "missing-compiler")
    assert required(cases.with_suffix(".gbin"), 103, True, environment) == reference
    nested = work / "nested.goat"
    nested.write_text('const identity=func(s){return s;}; '
                      'const forward=func(s){return identity(s);}; '
                      'println(forward("nested"));', encoding="utf-8")
    assert required(nested, 1, bound=2) == b"nested\n"
    # Concatenation allocates through the host context; the VM must copy its result.
    concat = work / "concat.goat"
    shutil.copyfile(ROOT / "example/native_string_concat.goat", concat)
    assert required(concat, 1) == expected
    generated = run("--print-c", concat).decode()
    assert "goat_native_string_concat(g_context," in generated
    assert "GOAT_NATIVE_PURE" in generated
    concat.write_text('''const concatenate=func(a,b){return a+b;};
const chain=func(a,b){var c=a+b; return concatenate(c,a)+b;};
var a="left"+chr(0); var b="😀right";
var result=concatenate(a,b); a="changed"; b="changed";
println(result=="left"+chr(0)+"😀right");
println(concatenate("", "")=="");
println(concatenate("", "right")=="right");
println(concatenate("left", "")=="left");
println(chain("a", "b")=="abab");
for(var i=0;i<100;i=i+1) {
    if(concatenate("x"+chr(0), "y")!="x"+chr(0)+"y") throw "mismatch";
}
''', encoding="utf-8")
    reference = b"true\n" * 5
    assert run("--native", "off", concat) == reference
    assert required(concat, 105, bound=2) == reference
    assert run("--compile", "--native", "required", concat) == b""
    concat.unlink()
    assert required(concat.with_suffix(".gbin"), 105, True, environment, bound=2) == reference
    print("Native string identity/concatenation: required dispatch, lifetime, Unicode/NUL/empty and saved execution OK")


if __name__ == "__main__":
    main()
