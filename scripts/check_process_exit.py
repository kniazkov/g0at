#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Coordinated non-unwinding shutdown, blocked streams, and one shared flush deadline."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

exe = str(Path(sys.argv[1]).resolve())
subprocess.run([exe, 'arbiter'], check=True, timeout=10)
assert subprocess.run([exe, 'unprepared'], timeout=5).returncode == 83
subprocess.run([exe, 'prepare-race'], check=True, timeout=10)
count = 3
if len(sys.argv) > 2:
    failure_exe = str(Path(sys.argv[2]).resolve())
    assert subprocess.run([failure_exe, 'failed-prepare'], timeout=5).returncode == 91
    count += 1
if len(sys.argv) > 3:
    delayed_exe = str(Path(sys.argv[3]).resolve())
    started = time.monotonic()
    assert subprocess.run([delayed_exe, 'failed-prepare'], timeout=8).returncode == 91
    assert 0.75 <= time.monotonic() - started < 5
    count += 1


def run(mode, code, release_delay=0):
    global count
    with tempfile.TemporaryDirectory(prefix='goat-process-exit-') as directory:
        marker = Path(directory) / 'ready'
        process = subprocess.Popen([exe, mode, str(marker)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            while not marker.exists():
                if process.poll() is not None:
                    out, err = process.communicate()
                    raise AssertionError((mode, process.returncode, out, err))
                assert time.monotonic() < deadline, (mode, 'readiness signal missing')
                time.sleep(0.005)
            # Readiness is an explicit signal, not a scheduler-delay assumption.
            # Shared-budget's worker starts shutdown after this signal; the
            # delay below exercises its deadline rather than establishing order.
            started = time.monotonic()
            if release_delay:
                time.sleep(release_delay)
            if mode == 'pipe-error':
                process.stdout.close()
            process.stdin.write(b'x')
            process.stdin.flush()
            process.wait(timeout=8)
            elapsed = time.monotonic() - started
            process.stdin.close()
            out = b'' if mode == 'pipe-error' else process.stdout.read()
            err = process.stderr.read()
            assert process.returncode in code, (mode, process.returncode, out, err)
            assert b'ATEXIT' not in out + err, (mode, 'CRT finalization ran')
            if mode == 'fast':
                assert out == b'stdout-without-newline', (out, err)
                assert err == b'stderr-without-newline', (out, err)
            elif mode.startswith(('write-', 'lock-')):
                assert 0.75 <= elapsed < 5, (mode, elapsed)
            elif mode == 'shared-budget':
                # Releasing stdout at 0.8 s then blocking stderr cannot acquire
                # another second; a repeated exit(9) cannot extend or replace 73.
                # Allow scheduler jitter, not a distinct per-stream deadline.
                assert 0.75 <= elapsed < 1.65, (mode, elapsed)
            count += 1
            return elapsed
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=8)
            for stream in [process.stdin, process.stdout, process.stderr]:
                if stream and not stream.closed:
                    stream.close()


fast = [run('fast', {255}) for _ in range(3)]
assert min(fast) < 0.9, ('fast flush waits the full deadline', fast)
run('error', {77})
run('pipe-error', {77})
for mode in ['write-stdout', 'write-stderr', 'write-both', 'lock-stdout', 'lock-stderr']:
    run(mode, {73})
run('shared-budget', {73}, release_delay=0.8)
for _ in range(8):
    run('race', {0, 7})
print(f'Process exit: {count} coordinated arbitration/output/deadline scenarios passed')
