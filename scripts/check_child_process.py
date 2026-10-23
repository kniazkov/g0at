#!/usr/bin/env python3
"""Portable process primitives; every fixture has an independent external watchdog."""
import ctypes
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

fixture = str(Path(sys.argv[1]).resolve())
goat = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None

def run(mode, stdin=b"Q", **kwargs):
    completed = subprocess.run([fixture, mode], input=stdin, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=12, **kwargs)
    assert completed.returncode == 0, (mode, completed.returncode, completed.stderr)
    value = json.loads(completed.stdout)
    assert value["cleanup"] == 0, (mode, value)
    if mode != "missing":
        assert value["error"] == 0, (mode, value)
    return value

for mode, code in [("empty", 0), ("exit", 23), ("stdin", 0), ("inherit", 0), ("parent", 0)]:
    value = run(mode)
    assert value["status"] == 1 and value["code"] == code, (mode, value)
    if mode == "empty":
        assert not value["out"] and not value["err"]
        assert not value["outTruncated"] and not value["errTruncated"]
for mode, size in [("exact", 65536), ("over", 65537), ("large", 1048576)]:
    value = run(mode)
    prefix = bytes(range(256)) * 256
    assert value["status"] == 1 and value["code"] == 0
    assert bytes.fromhex(value["out"]) == bytes.fromhex(value["err"]) == prefix
    assert value["outTruncated"] == value["errTruncated"] == (size > 65536)
value = run("args")
assert bytes.fromhex(value["result"]).decode("utf-8").split("\0") == [
    "", "a b", 'a"b', "tail\\", "&%x%!", "Привет😀", ""]
assert run("missing")["status"] == 5
assert run("expired")["status"] == 3
assert run("stop")["status"] == 4
for mode in ["pause", "continuous", "budget", "tree"]:
    value = run(mode)
    assert value["status"] == 3 and value["ms"] < 6000, (mode, value)
    if mode == "continuous":
        assert value["out"] and value["err"]
for mode in ["crash", "tree-exit", "tree-fail", "tree-crash"]:
    value = run(mode)
    if mode.endswith("exit"):
        assert value["status"] == 1 and value["code"] == 0
    elif mode.endswith("fail"):
        assert value["status"] == 1 and value["code"] == 23
    else:
        assert value["status"] == 2 or (value["status"] == 1 and value["code"] != 0)
    assert value["result"], mode  # output survives crashes too

# Readiness comes from the worker only after both descendants have confirmed startup.
def start_owner(mode):
    process = subprocess.Popen([fixture, mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lines = queue.Queue()
    def collect():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)
    threading.Thread(target=collect, daemon=True).start()
    return process, lines

def ready(lines, marker=b"READY\n"):
    pids = []
    while True:
        line = lines.get(timeout=8)
        assert line is not None, pids
        if line == marker:
            return pids
        pids.append(int(line))

def alive(pid):
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, pid)
        if not handle:
            return False
        result = kernel.WaitForSingleObject(handle, 0)
        kernel.CloseHandle(handle)
        return result == 258
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False

def gone(pids):
    end = time.monotonic() + 6
    while any(alive(pid) for pid in pids):
        assert time.monotonic() < end, ("surviving processes", pids)
        time.sleep(0.01)

owner, lines = start_owner("owner")
other, other_lines = start_owner("owner")
try:
    owned = ready(lines)
    independent = ready(other_lines)
    assert len(owned) == len(independent) == 3
    # A separate successful/crashing probe cannot stop either independent tree.
    for mode in ["tree-exit", "tree-fail", "tree-crash", "tree"]:
        value = run(mode)
        gone([int(p) for p in bytes.fromhex(value["result"]).splitlines()])
        assert all(alive(pid) for pid in owned + independent)
    owner.kill()
    owner.wait(timeout=8)
    gone(owned)
    assert lines.get(timeout=8) is None
    assert all(alive(pid) for pid in independent)
finally:
    for process in [owner, other]:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=8)
gone(independent)

# Stable parent reference, in an explicitly independent cleanup-service process.
owner, lines = start_owner("watch-owner")
try:
    ready(lines, b"WATCH_READY\n")
    owner.kill()
    owner.wait(timeout=8)
    assert lines.get(timeout=8) == b"PARENT_GONE\n"
    assert lines.get(timeout=8) is None
finally:
    if owner.poll() is None:
        owner.kill()
        owner.wait(timeout=8)

# Owner death in the launch window: output pipe closure is also externally bounded.
for _ in range(20):
    owner, lines = start_owner("owner")
    owner.kill()
    owner.wait(timeout=8)
    observed = []
    while True:
        line = lines.get(timeout=8)
        if line is None:
            break
        if line.strip().isdigit():
            observed.append(int(line))
    gone(observed)

# Invalid timeout syntax is rejected before opening an input file; valid values parse.
if goat:
    for value in ["0", "-1", "1.5", "", "+2", "4294967296", "999999999999999999999"]:
        result = subprocess.run([goat, "--native-probe-timeout", value, "missing.goat"],
                                capture_output=True, timeout=8)
        assert result.returncode != 0 and b"timeout" in result.stderr.lower(), (value, result.stderr)
print("Child process tests passed: capture, deadlines, tree ownership, parent death, stdin, arguments.")
