#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Verify indexed-access and array and inheritance fixtures, regenerated source and saved bytecode."""
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
    env = dict(os.environ, GOAT_LANGUAGE="en", CC="goat-index-test-no-compiler")

    def run(*options):
        result = subprocess.run([str(goat), *map(str, options)], env=env,
                                text=True, encoding="utf-8", capture_output=True, timeout=30)
        assert result.returncode == 0 and not result.stderr, (
            result.args, result.returncode, result.stdout, result.stderr)
        return result.stdout

    count = 0
    for fixture in sorted([*fixtures.glob("index_*"), *fixtures.glob("array_*"), *fixtures.glob("inheritance_*"), *fixtures.glob("loops_*"), *fixtures.glob("switch_*"), *fixtures.glob("compound_*"), *fixtures.glob("iteration_*"), *fixtures.glob("flat_*"), *fixtures.glob("conditional_*"), *fixtures.glob("finally_*"), *fixtures.glob("access_*")]):
        source = fixture / "program.goat"
        if not source.is_file():
            continue
        expected = (fixture / "expected_output.txt").read_text(encoding="utf-8")
        for optimization in ("none", "all"):
            directory = output / (fixture.name + "-" + optimization)
            directory.mkdir(exist_ok=True)
            copied = directory / "program.goat"
            copied.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            assert run("--optimize", optimization, copied) == expected, copied
            regenerated = run("--optimize", optimization, "--compile",
                              "--print-source-code", copied)
            restored = directory / "restored.goat"
            restored.write_text(regenerated, encoding="utf-8")
            assert run("--optimize", optimization, restored) == expected, restored
            copied.unlink()
            assert run("--run", "--native", "off", copied.with_suffix(".gbin")) == expected, copied
        count += 1
        print("[ ok ]", fixture.name)
    assert count, "No index fixtures found"
    # Modifiers belong to declarations, never expressions or existing properties.
    invalid = ("public private x = 1;", "public public x = 1;",
               "var public x = 1;", "const private x = 1;", "public;",
               "public x;", "var obj = {}; public obj.x = 1;",
               "var x = public 1;", "public println(1);")
    for language in ("en", "ru"):
        for index, source in enumerate(invalid):
            path = output / f"invalid-access-{language}-{index}.goat"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(goat), "--compile", str(path)],
                                    env=dict(env, GOAT_LANGUAGE=language), text=True,
                                    encoding="utf-8", capture_output=True, timeout=30)
            assert result.returncode != 0 and result.stderr, (source, result)
    print(f"Index checks passed: {count} fixtures, both optimization levels, source and binary round trips")


if __name__ == "__main__":
    main()
