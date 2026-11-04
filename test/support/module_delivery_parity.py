#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Temporary #388 gate: unchanged original and C autonomous delivery parity.

The original module is executed unchanged, including every assertion. Hooks only
retain its temporary workspace and check source/descriptor writes against frozen
inputs. Neither subprocess behavior nor producer/consumer results are mocked.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_fixtures(data):
    fixtures = data / "module-delivery"
    manifest = json.loads((fixtures / "manifest.json").read_text(encoding="utf-8"))
    script = ROOT / manifest["original"]
    require(hashlib.sha256(script.read_bytes().replace(b"\r\n", b"\n")).hexdigest() == manifest["originalSha256"],
            "original checker changed since the frozen baseline")
    for name, expected in manifest["files"].items():
        require(hashlib.sha256((fixtures / name).read_bytes()).hexdigest() == expected,
                f"frozen input changed: {name}")
    return fixtures, manifest


def capture_original(args):
    fixtures, _ = check_fixtures(args.data)
    sys.path.insert(0, str(ROOT / "scripts"))
    import module_execution_support
    original_put = module_execution_support.put
    source_bytes = {name: (fixtures / name).read_bytes() for name in
                    ("leaf2.goat", "leaf1.goat", "middle.goat", "delivery.goat", "alternate.goat")}
    source_counts = collections.Counter()
    descriptors = collections.Counter()
    retained = []

    def put(path, text):
        path = Path(path)
        raw = text.encode("utf-8")
        if path.name == "main.goat":
            matches = [name for name, expected in source_bytes.items() if raw == expected]
            require(len(matches) == 1, f"original generated an unfrozen source: {path}")
            source_counts[matches[0]] += 1
        elif path.name == "module.gmod":
            expected = fixtures / "descriptors" / path.parent.name / "module.gmod"
            require(expected.is_file() and expected.read_bytes() == raw,
                    f"original descriptor differs from fixture: {path}")
            descriptors[path.parent.name] += 1
        else:
            raise RuntimeError(f"unexpected original put: {path}")
        return original_put(path, text)

    class RetainedTemporaryDirectory:
        def __init__(self, *unused, **kwargs):
            require(not retained, "original unexpectedly allocated multiple workspaces")
            require(kwargs == {"prefix": "goat-delivery-"},
                    f"original temporary directory contract changed: {kwargs}")
            self.name = str(args.capture / "original work")
            Path(self.name).mkdir()
            retained.append(self.name)

        def __enter__(self):
            return self.name

        def __exit__(self, *unused):
            return False

    module_execution_support.put = put
    tempfile.TemporaryDirectory = RetainedTemporaryDirectory
    sys.argv = [str(ROOT / "scripts/check_module_delivery.py"), str(args.goat),
                str(args.inspect), args.cc]
    # run_path compiles the original bytes; no rewriting, skipped asserts,
    # subprocess substitutes, alternate oracle or generated expected output.
    result = runpy.run_path(sys.argv[0], run_name="__main__")
    wanted_sources = {"leaf2.goat": 1, "leaf1.goat": 1, "middle.goat": 1,
                      "delivery.goat": 5 if os.name == "nt" else 3, "alternate.goat": 1}
    require(source_counts == wanted_sources, f"source invocation coverage changed: {source_counts}")
    require(result["expected"].encode() == (fixtures / "expected.txt").read_bytes(),
            "original stdout oracle differs from frozen input")
    require(result["COUNT"] == (71 if os.name == "nt" else 49),
            f"original command coverage changed: {result['COUNT']}")
    report = {"sourceCounts": dict(source_counts), "descriptorCounts": dict(descriptors),
              "invocations": result["COUNT"], "work": retained[0]}
    (args.capture / "original-capture.json").write_text(json.dumps(report, indent=2) + "\n",
                                                       encoding="utf-8")
    return 0


def execute(command, directory, label, timeout, host, data):
    # The shared C host owns this whole phase tree and deadline. Do not add a
    # Python timeout that can kill the host before it finishes owned cleanup.
    command = [str(host), str(directory), str(data), str(timeout), *command]
    with (directory / f"{label}.stdout").open("wb") as out, \
            (directory / f"{label}.stderr").open("wb") as err:
        result = subprocess.run(command, stdout=out, stderr=err, check=False)
    require(result.returncode == 0,
            f"{label} failed with exit {result.returncode}; see {directory}")


def archive_observation(path):
    with zipfile.ZipFile(path) as archive:
        info = archive.infolist()
        require(len({part.filename for part in info}) == len(info), f"duplicate member: {path}")
        members = {part.filename: archive.read(part) for part in info}
    # Invalid JSON is an intentionally frozen negative and must stay exact.
    raw = members["module.json"]
    try:
        metadata = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError):
        require(path.name == "json.gbin" and raw == b"{bad json",
                f"unexpected invalid metadata: {path}")
        metadata = None
    native = {}
    if metadata is not None:
        for variant in metadata.get("native", []):
            body = members[variant["library"]]
            native[variant["library"]] = {
                "matchesRecordedHash": hashlib.sha256(body).hexdigest() == variant.pop("hash"),
                "corruptSuffix": body.endswith(b"changed native payload"),
                "nonempty": bool(body),
            }
    # Actual DLL/SO bytes depend on the independent producer's temporary paths
    # (and Windows PE timestamps). Compare their target/ABI/provenance metadata
    # and validation state, never demand equal build-directory-dependent hashes.
    portable = {name: data.hex() for name, data in members.items()
                if name != "module.json" and not name.startswith("native/")}
    return {"metadata": metadata,
            "invalidMetadata": None if metadata is not None else raw.hex(),
            "members": [(part.filename, part.compress_type) for part in info],
            "portable": portable, "native": native}


def compare_trees(original, converted):
    old = {p.relative_to(original): p for p in original.rglob("*.gbin")}
    new = {p.relative_to(converted): p for p in converted.rglob("*.gbin")}
    require(old.keys() == new.keys(),
            f"delivered graph differs: missing={old.keys() - new.keys()}, extra={new.keys() - old.keys()}")
    require(len(old) >= 12, "delivery graph unexpectedly incomplete")
    for name in sorted(old):
        left, right = archive_observation(old[name]), archive_observation(new[name])
        require(left == right, f"Python/C archive differs: {name}\nPython={left}\nC={right}")


def compare_commands(original, converted):
    original_report = json.loads((original / "original-capture.json").read_text(encoding="utf-8"))
    execution_log = (converted / "execution.log").read_text(encoding="utf-8")
    commands = [int(number) for number in re.findall(
        r"^command ([0-9]+) expected_exit=", execution_log, re.MULTILINE)]
    expected_count = original_report["invocations"]
    require(commands == list(range(1, expected_count + 1)),
            f"Python/C command coverage differs: original={expected_count}, C={commands}")
    require(execution_log.rstrip().endswith("PASS"), "C execution log did not finish successfully")


def counters(directory):
    result = collections.Counter()
    for path in directory.glob("counters-*.txt"):
        fields = dict(line.split("=", 1) for line in path.read_text(encoding="utf-8").splitlines())
        require(set(fields) == {"mode", "preparation", "bound", "omitted", "attempts",
                               "succeeded", "retries"}, f"unexpected counters: {path}")
        result[tuple(sorted(fields.items()))] += 1
    require(result, f"no dispatch counters retained: {directory}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("goat", type=Path)
    parser.add_argument("inspect", type=Path)
    parser.add_argument("merge", type=Path)
    parser.add_argument("cc")
    parser.add_argument("runner", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("format", type=Path)
    parser.add_argument("--phase-timeout", type=float, default=600)
    parser.add_argument("--capture", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    root = None
    try:
        for name in ("goat", "inspect", "merge", "runner", "build", "data", "format"):
            setattr(args, name, getattr(args, name).resolve())
        args.cc = str(Path(shutil.which(args.cc) or args.cc).resolve())
        expected_merge = args.inspect.with_name("goat-merge.exe" if os.name == "nt" else "goat-merge")
        require(args.merge == expected_merge, "original and C must use the same inspector-sibling merge tool")
        require(math.isfinite(args.phase_timeout) and 0 < args.phase_timeout <= 86400,
                "phase timeout must be finite, positive and at most 86400 seconds")
        check_fixtures(args.data)
        if args.capture is not None:
            return capture_original(args)
        args.build.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="scenario-module-delivery-parity-", dir=args.build))
        host = args.runner.with_name("module_delivery_parity_host" + args.runner.suffix)
        require(host.is_file(), f"owned-process parity host missing: {host}")
        original, converted = root / "original", root / "converted"
        original.mkdir()
        converted.mkdir()
        print(f"Module delivery parity artifacts: {root}", flush=True)
        common = [str(args.goat), str(args.inspect), str(args.merge), args.cc,
                  str(args.runner), str(args.build), str(args.data), str(args.format)]
        execute([sys.executable, str(Path(__file__).resolve()), *common,
                 "--capture", str(original)], root, "original", args.phase_timeout, host, args.data)
        execute([str(args.runner), "integration.module_delivery.end-to-end",
                 str(args.goat), str(args.inspect), str(args.merge), args.cc,
                 str(converted), str(args.data), str(args.format), "--keep-work"], root, "converted", args.phase_timeout, host, args.data)
        old = original / "original work"
        work = list(converted.glob("scenario-*/delivery work"))
        require(len(work) == 1, f"expected one retained C delivery workspace: {work}")
        new = work[0]
        compare_commands(original, new.parent)
        compare_trees(old / "delivered", new / "delivered")
        require(counters(old) == counters(new), "Python/C dispatch execution coverage differs")
        for directory in (old, new):
            require(not list(directory.rglob("*.goat")) and not list(directory.rglob("*.gmod")),
                    f"producer sources survived delivery: {directory}")
        check_fixtures(args.data)
        print("[ ok ] original/C frozen inputs, complete command coverage, graph, archive and dispatch parity", flush=True)
        return 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, AssertionError, subprocess.TimeoutExpired) as error:
        print(f"[fail] {error}; retained parity artifacts: {root}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
