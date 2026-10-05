#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Check loop syntax, source round trips, and persisted VM/native execution."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("goat", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    goat = args.goat.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent.parent / "test" / "functional"
    environment = dict(os.environ, GOAT_LANGUAGE="en")

    def run(*options, env=None):
        return subprocess.run(
            [str(goat), *map(str, options)], text=True, encoding="utf-8",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
            env=environment if env is None else env,
        )

    def check(result, expected, status=0, error=""):
        assert (result.returncode, result.stdout, result.stderr) == (status, expected, error), (
            result.args, result.returncode, result.stdout, result.stderr
        )

    passed = 0
    for fixture in sorted(fixtures.glob("for_*")):
        source = fixture / "program.goat"
        if not source.is_file():
            continue
        error_file = fixture / "expected_error.txt"
        for optimization in ("none", "all"):
            if error_file.exists():
                check(run("--optimize", optimization, source), "", 1,
                      error_file.read_text(encoding="utf-8"))
                continue
            expected = (fixture / "expected_output.txt").read_text(encoding="utf-8")
            directory = output / (fixture.name + "-" + optimization)
            directory.mkdir(exist_ok=True)
            copied = directory / "program.goat"
            copied.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            check(run("--optimize", optimization, copied), expected)
            printed = run("--optimize", optimization, "--compile", "--print-source-code", copied)
            assert printed.returncode == 0 and not printed.stderr, printed
            restored = directory / "restored.goat"
            restored.write_text(printed.stdout, encoding="utf-8")
            check(run("--optimize", optimization, restored), expected)
            check(run("--run", "--native", "off", copied.with_suffix(".gbin")), expected)
        passed += 1
        print("[ ok ]", fixture.name)

    # Analysis and compilation must finish even for a source loop that never terminates.
    infinite = output / "infinite.goat"
    infinite.write_text('for (;;) ; println("unreachable");\n', encoding="utf-8")
    check(run("--compile", infinite), "")
    print("[ ok ] compile an infinite loop without executing it")

    native_source = output / "native.goat"
    native_source.write_text((fixtures / "native_modes" / "loops.goat").read_text(encoding="utf-8"),
                             encoding="utf-8")
    check(run("--compile", "--native", "required", native_source), "")
    native_source.unlink()
    report = output / "native.report"
    # Running the saved pair must not need either source or the compiler.
    without_compiler = dict(environment, CC="goat-missing-compiler-for-test")
    expected = (fixtures / "native_modes" / "loops.out").read_text(encoding="utf-8")
    check(run("--run", "--native", "required", "--save-native", report,
              native_source.with_suffix(".gbin"), env=without_compiler), expected)
    fields = dict(line.split("=", 1) for line in report.read_text(encoding="utf-8").splitlines())
    assert fields["preparation"] == "ready" and int(fields["succeeded"]) == 12, fields
    assert int(fields["attempts"]) == 12 and int(fields["retries"]) == 0, fields
    print("[ ok ] persisted native loops, all entries succeed without VM retry")
    # Long loops and many shallow calls must not exhaust a cumulative native budget.
    long_source = output / "long-native.goat"
    long_source.write_text((fixtures / "native_modes" / "long_loops.goat").read_text(encoding="utf-8"),
                           encoding="utf-8")
    check(run("--compile", "--native", "required", long_source), "")
    long_source.unlink()
    check(run("--run", "--native", "required", "--save-native", report,
              long_source.with_suffix(".gbin"), env=without_compiler),
          "499999500000\n500000500000\n750000\n")
    fields = dict(line.split("=", 1) for line in report.read_text(encoding="utf-8").splitlines())
    assert fields["preparation"] == "ready", fields
    assert int(fields["attempts"]) == int(fields["succeeded"]) == 3, fields
    assert int(fields["retries"]) == 0, fields
    print("[ ok ] million-iteration loops and shallow call trees remain native")
    print(f"For-loop checks passed: {passed} fixtures and 3 integration checks")


if __name__ == "__main__":
    main()
