#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compare precompiled VM and native call trees; compilation is never timed."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

DEPTH = 12
TREES = 1024
SAMPLES = 5
CALLS_PER_TREE = (1 << DEPTH) - 1


def program():
    lines = ["const leaf = func(n) { return n * n + 3 * n + 7; }"]
    previous = "leaf"
    for level in range(1, DEPTH):
        name = f"level{level}"
        lines.append(f"const {name} = func(n) {{ return {previous}(n + 1) + {previous}(n + 2); }}")
        previous = name
    lines.append("var total = 0")
    for index in range(TREES):
        lines.append(f"total = total + {previous}({index})")
    lines.append('print(total); print("\\n");')
    return "\n".join(lines) + "\n"


def expected_result():
    # Sum over the binomial distribution of leaf arguments, independently of Goat.
    from math import comb
    levels = DEPTH - 1
    return sum(comb(levels, twos) * (n * n + 3 * n + 7)
               for start in range(TREES)
               for twos in range(levels + 1)
               for n in [start + levels + twos])


def execute(command, env):
    start = time.perf_counter_ns()
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=180)
    elapsed = time.perf_counter_ns() - start
    if result.returncode or result.stderr:
        raise RuntimeError(f"command failed: {command}\n"
                           f"exit={result.returncode}\n{result.stderr.decode('utf-8', errors='replace')}")
    return result.stdout.replace(b"\r\n", b"\n"), elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("interpreter", type=Path, help="Release goat executable")
    parser.add_argument("output", type=Path, help="directory for source, binaries and timing report")
    args = parser.parse_args()
    executable = args.interpreter.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    print("Starting native performance testing...", flush=True)
    try:
        source = program()
        expected = f"{expected_result()}\n".encode("ascii")
        env = os.environ.copy()
        # Compile both inputs first; neither preparation nor warm-up enters the samples.
        for mode in ("vm", "native"):
            path = output / f"{mode}.goat"
            path.write_text(source, encoding="utf-8")
            stdout, _ = execute([str(executable), "--lang", "en", "--compile", "--native",
                                 "off" if mode == "vm" else "required", str(path)], env)
            if stdout:
                raise RuntimeError("compilation executed the program or produced unexpected output")
        # Execution cannot silently invoke a compiler or read its original source.
        for mode in ("vm", "native"):
            (output / f"{mode}.goat").unlink()
        (output / "call_tree.goat").write_text(source, encoding="utf-8")
        env["CC"] = "goat-no-such-compiler"
        times = {"vm": [], "native": []}

        def run(mode, label):
            report = output / f"{mode}-{label}.report"
            stdout, elapsed = execute([str(executable), "--lang", "en", "--run", "--native",
                                       "off" if mode == "vm" else "required", "--save-native",
                                       str(report), str(output / f"{mode}.gbin")], env)
            if stdout != expected:
                raise RuntimeError(f"{mode}: wrong result {stdout!r}, expected {expected!r}")
            fields = dict(line.split("=", 1) for line in report.read_text(encoding="utf-8").splitlines())
            wanted = {"preparation": "disabled" if mode == "vm" else "ready",
                      "attempts": "0" if mode == "vm" else str(TREES),
                      "succeeded": "0" if mode == "vm" else str(TREES), "retries": "0"}
            if any(fields.get(key) != value for key, value in wanted.items()):
                raise RuntimeError(f"{mode}: unexpected native dispatch: {fields}")
            return elapsed

        run("vm", "warmup")
        run("native", "warmup")
        for sample in range(SAMPLES):
            # Start with VM; alternate pair order to reduce systematic scheduling bias.
            for mode in (("vm", "native") if sample % 2 == 0 else ("native", "vm")):
                times[mode].append(run(mode, str(sample + 1)))
        vm = statistics.median(times["vm"])
        native = statistics.median(times["native"])
        result = {"depth": DEPTH, "trees": TREES, "calls_per_tree": CALLS_PER_TREE,
                  "total_calls": TREES * CALLS_PER_TREE, "expected_result": expected_result(),
                  "samples_ns": times, "vm_median_ns": vm, "native_median_ns": native,
                  "speedup": vm / native, "passed": native < vm}
        (output / "timings.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"VM median: {vm / 1e9:.6f} s; native median: {native / 1e9:.6f} s; "
              f"speedup: {vm / native:.2f}x", flush=True)
        if native >= vm:
            raise RuntimeError("native execution is not faster than VM execution")
        print("[ ok ] precompiled native call tree is faster than VM")
        print("Native performance testing done; total: 1, passed: 1, failed: 0")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"[fail] {error}")
        print("Native performance testing done; total: 1, passed: 0, failed: 1")
        return 1


if __name__ == "__main__":
    sys.exit(main())
