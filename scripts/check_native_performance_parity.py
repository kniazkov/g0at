#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Temporary #387 gate: run unchanged Python and C benchmarks on identical inputs."""
import argparse
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


INVARIANTS = {
    "depth": 12,
    "trees": 1024,
    "calls_per_tree": 4095,
    "total_calls": 4193280,
    "expected_result": sum(
        math.comb(11, twos) * (n * n + 3 * n + 7)
        for start in range(1024)
        for twos in range(12)
        for n in [start + 11 + twos]
    ),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def execute(command, directory, label):
    # The original owns its unchanged 180-second command deadlines, and C owns
    # its scenario/command deadlines. CTest supplies the outer cleanup margin.
    with (directory / f"{label}.stdout").open("wb") as out, \
            (directory / f"{label}.stderr").open("wb") as err:
        result = subprocess.run(command, stdout=out, stderr=err, check=False)
    require(result.returncode == 0,
            f"{label} failed with exit {result.returncode}; see {directory}")


def report(path, mode):
    fields = dict(line.split("=", 1)
                  for line in path.read_text(encoding="utf-8").splitlines())
    wanted = {"preparation": "disabled" if mode == "vm" else "ready",
              "attempts": "0" if mode == "vm" else "1024",
              "succeeded": "0" if mode == "vm" else "1024", "retries": "0"}
    require(all(fields.get(key) == value for key, value in wanted.items()),
            f"incorrect dispatch report: {path}")
    return {key: fields[key] for key in wanted}


def timings(directory):
    path = directory / "timings.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(data, dict), f"timings object missing: {path}")
    require(set(data) == set(INVARIANTS) | {
        "samples_ns", "vm_median_ns", "native_median_ns", "speedup", "passed"},
        f"timings schema mismatch: {path}")
    for key, expected in INVARIANTS.items():
        require(type(data[key]) is int and data[key] == expected,
                f"incorrect {key}: {path}")
    samples = data["samples_ns"]
    require(isinstance(samples, dict) and set(samples) == {"vm", "native"},
            f"incorrect sample modes: {path}")
    for mode in ("vm", "native"):
        values = samples[mode]
        require(isinstance(values, list) and len(values) == 5
                and all(type(value) is int and value > 0 for value in values),
                f"expected five positive integer {mode} samples: {path}")
        median = data[f"{mode}_median_ns"]
        require(type(median) is int and median == statistics.median(values),
                f"incorrect {mode} median: {path}")
        require(not (directory / f"{mode}.goat").exists(),
                f"original source was not deleted: {directory}")
        require((directory / f"{mode}.gbin").is_file(),
                f"precompiled {mode} input missing: {directory}")
    speedup = data["speedup"]
    require(type(speedup) in (int, float) and math.isfinite(speedup)
            and math.isclose(speedup, data["vm_median_ns"] / data["native_median_ns"],
                             rel_tol=1e-12, abs_tol=0.0),
            f"incorrect speedup: {path}")
    require(type(data["passed"]) is bool
            and data["passed"] == (data["native_median_ns"] < data["vm_median_ns"])
            and data["passed"], f"performance comparison failed: {path}")
    return data


def compare(original, converted):
    a = (original / "call_tree.goat").read_bytes().replace(b"\r\n", b"\n")
    b = (converted / "call_tree.goat").read_bytes().replace(b"\r\n", b"\n")
    require(a == b, "Python and C call-tree inputs differ")
    left, right = timings(original), timings(converted)
    for key in INVARIANTS:
        require(left[key] == right[key], f"Python/C invariant differs: {key}")
    for mode in ("vm", "native"):
        for label in ("warmup", "1", "2", "3", "4", "5"):
            name = f"{mode}-{label}.report"
            require(report(original / name, mode) == report(converted / name, mode),
                    f"Python/C dispatch differs: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("goat", type=Path)
    parser.add_argument("runner", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("data", type=Path)
    args = parser.parse_args()
    root = None
    try:
        build = args.build.resolve()
        build.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="native-performance-parity-", dir=build))
        original, converted = root / "original", root / "converted"
        original.mkdir()
        converted.mkdir()
        print(f"Native performance parity artifacts: {root}", flush=True)
        script = Path(__file__).resolve().with_name("check_native_performance.py")
        execute([sys.executable, str(script), str(args.goat.resolve()), str(original)],
                root, "original")
        before = set(converted.glob("scenario-*"))
        execute([str(args.runner.resolve()), "integration.native_performance.benchmark",
                 str(args.goat.resolve()), str(converted), str(args.data.resolve()),
                 "--keep-work"], root, "converted")
        work = set(converted.glob("scenario-*")) - before
        require(len(work) == 1, "expected exactly one retained C scenario workspace")
        compare(original, work.pop())
        print("[ ok ] Python/C input, result, dispatch and timing-report parity", flush=True)
        return 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f"[fail] {error}; retained parity artifacts: {root}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
