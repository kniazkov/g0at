#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Exercise real compiler discovery, cache keys, fallback, and bounded probes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
compiler = shutil.which(sys.argv[2] if len(sys.argv) > 2 else os.environ.get("CC", "gcc"))
assert compiler, "a real C compiler is required"
compiler = str(Path(compiler).resolve())
passed = 0


def check(name, condition):
    global passed
    assert condition, name
    passed += 1
    print("[ ok ]", name)


with tempfile.TemporaryDirectory(prefix="goat-toolchain-tests-") as directory:
    root = Path(directory)
    suffix = ".exe" if os.name == "nt" else ""
    wrapper = root / ("controlled-compiler" + suffix)
    shutil.copy2(exe, wrapper)
    log = root / "calls.log"
    env = dict(os.environ, GOAT_TOOLCHAIN_REAL=compiler, GOAT_TOOLCHAIN_LOG=str(log))

    def run(cc=str(wrapper), target="host", timeout="10", repetitions="1", mode="select", extra=None):
        current = dict(env)
        current.update(extra or {})
        result = subprocess.run([str(exe), mode, cc, target, timeout, repetitions],
                                env=current, text=True, capture_output=True, timeout=120)
        assert result.returncode == 0, (result.stdout, result.stderr)
        return result.stdout

    output = run(repetitions="2")
    check("real compiler probe succeeds without loading", output.count("available=1") == 2)
    calls = log.read_text().splitlines()
    check("same probe in one context uses cache", sum(" -std=c11" in line for line in calls) == 2)
    output = run()
    check("new process reprobes", len(log.read_text().splitlines()) > len(calls))
    log.write_text("")
    output = run(repetitions="2", mode="vary-timeout")
    check("different timeout has a distinct cache key", sum(" -std=c11" in line for line in log.read_text().splitlines()) == 4)
    check("compiler name/version captured", "name=GCC version=" in output or "name=Clang version=" in output)
    check("unsupported target never becomes host", "unsupported native target" in run(target="linux-unknown-gnu"))
    check("invalid zero timeout rejected", "positive representable" in run(timeout="0"))
    check("absent explicit compiler stays absent", "explicit C compiler was not found" in run(cc=str(root / "absent")))
    output = run(extra={"GOAT_TOOLCHAIN_MODE": "link-fail"})
    check("preprocessing success does not imply working linker", "available=0" in output and "controlled link failure" in output)
    for mode in ("hang-query", "hang-link"):
        output = run(timeout="1", extra={"GOAT_TOOLCHAIN_MODE": mode})
        check(mode + " bounded by shared deadline", "available=0 timeout=1" in output)
    output = run(target="host+avx2")
    if "unsupported native target" not in output:
        check("explicit AVX2 profile retains target suffix", "+avx2 available=1" in output)
    # Discover only controlled known names. GCC's internal tool search remains explicit.
    for name in ("cc", "gcc", "clang"):
        shutil.copy2(exe, root / (name + suffix))
    selected = "gcc" if os.name == "nt" else "cc"
    fallback = "clang" if os.name == "nt" else "gcc"
    log.write_text("")
    for tool in ("ld", "as"):
        location = shutil.which(tool)
        if location and os.name != "nt":
            (root / tool).symlink_to(Path(location).resolve())
    tool_paths = os.pathsep.join(filter(None, (os.path.dirname(compiler),
        os.path.dirname(shutil.which("ld") or ""))))
    isolated_path = str(root) + (os.pathsep + os.environ.get("PATH", "") if os.name == "nt" else "")
    isolation = {"PATH": isolated_path, "COMPILER_PATH": tool_paths}
    output = run(cc="PATH", extra=isolation)
    check("ordinary selection stops at first successful candidate", "selected=none" not in output and all(selected + suffix + " " in line for line in log.read_text().splitlines()))
    log.write_text("")
    output = run(cc="PATH", extra=dict(isolation, GOAT_TOOLCHAIN_FAIL=selected + suffix))
    check("rejected preferred candidate permits stable fallback", "selected=" + str(root / (fallback + suffix)) in output)
    output = run(cc="PATH", timeout="2", extra=dict(isolation,
        GOAT_TOOLCHAIN_MODE="hang-query", GOAT_TOOLCHAIN_MODE_FOR=selected + suffix))
    check("timed out candidate does not block fallback", "selected=" + str(root / (fallback + suffix)) in output and "timeout=1" in output)
    output = run(cc="PATH", extra={"PATH": str(root / "unsearched")})
    check("compiler outside PATH is not discovered by hidden scanning", "no supported C compiler" in output)
    output = run(cc="PATH", mode="list", target="host", extra=isolation)
    check("enumeration includes all successful compilers", output.count("available=1") >= 3)
    output = run(cc=str(root / (selected + suffix)), extra=dict(isolation, GOAT_TOOLCHAIN_FAIL=selected + suffix))
    check("explicit compiler failure never selects fallback", "selected=none" in output and "count=1" in output)
print(f"Native toolchain checks: {passed}/{passed}")
