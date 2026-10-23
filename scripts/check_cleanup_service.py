#!/usr/bin/env python3
"""Cleanup service acceptance tests; explicit readiness, process handles, external watchdogs."""
import ctypes
import os
from pathlib import Path
import queue
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time

fixture = str(Path(sys.argv[1]).resolve())
passed = 0


def until(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        assert time.monotonic() < deadline, "external fixture deadline expired"
        # Poll a predicate, never use elapsed sleep as evidence of completion/readiness.
        threading.Event().wait(.01)


class ProcessWatch:
    """Pin identity before signalling, including for an independently launched cleaner."""
    def __init__(self, pid):
        self.pid = pid
        if os.name == 'nt':
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
            self.kernel.OpenProcess.restype = ctypes.c_void_p
            self.kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            self.kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            self.kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            self.handle = self.kernel.OpenProcess(0x100001, False, pid)
            assert self.handle, ctypes.get_last_error()
        else:
            self.handle = os.pidfd_open(pid)

    def wait(self, timeout=10):
        if os.name == 'nt':
            assert self.kernel.WaitForSingleObject(self.handle, int(timeout * 1000)) == 0
        else:
            assert select.select([self.handle], [], [], timeout)[0]

    def kill(self):
        if os.name == 'nt':
            assert self.kernel.TerminateProcess(self.handle, 99)
        else:
            signal.pidfd_send_signal(self.handle, signal.SIGKILL)

    def close(self):
        if os.name == 'nt':
            self.kernel.CloseHandle(self.handle)
        else:
            os.close(self.handle)


class Owner:
    def __init__(self, root, name, mode='owner', fault=None):
        self.root = root / name
        self.root.mkdir()
        self.pid_file = self.root / 'cleaner.pid'
        self.done_file = self.root / 'cleaner.done'
        env = dict(os.environ, TMPDIR=str(self.root), TMP=str(self.root), TEMP=str(self.root),
                   GOAT_TEST_CLEANER_PID=str(self.pid_file), GOAT_TEST_CLEANER_DONE=str(self.done_file))
        if fault:
            env['GOAT_TEST_CLEANER_FAULT'] = fault
        self.process = subprocess.Popen([fixture, mode], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        env=env)
        self.lines = queue.Queue()
        def read_lines():
            for line in self.process.stdout:
                self.lines.put(line.decode('utf-8').rstrip('\r\n'))
            self.lines.put(None)
        threading.Thread(target=read_lines, daemon=True).start()
        self.cleaner = None
        self.directory = None

    def line(self, timeout=10):
        line = self.lines.get(timeout=timeout)
        if line is None:
            raise AssertionError(('unexpected owner EOF', self.process.poll(), self.process.stderr.read()))
        return line

    def pin(self):
        def read_pid():
            try:
                text = self.pid_file.read_text().strip()
                return int(text) if text else None
            except FileNotFoundError:
                return None
        self.cleaner = ProcessWatch(until(read_pid))

    def ready(self):
        line = self.line()
        assert line.startswith('ready '), line
        self.directory = Path(line[6:])
        assert self.directory.is_absolute() and self.directory.parent == self.root
        assert (self.directory / 'данные 😀.tmp').exists()
        self.pin()
        assert len(list(self.root.glob("goat-run-*"))) == 1

    def send(self, command):
        self.process.stdin.write((command + '\n').encode())
        self.process.stdin.flush()

    def ended(self, code=0):
        assert self.process.wait(timeout=10) == code, self.process.stderr.read()

    def cleaned(self, timeout=10):
        self.cleaner.wait(timeout)
        assert not self.directory.exists()
        assert self.done_file.read_text().strip() == '0'

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()
        if self.cleaner:
            self.cleaner.close()


def ok(name):
    global passed
    passed += 1
    print('[ ok ]', name, flush=True)


print('Starting cleanup service testing...', flush=True)
subprocess.run([fixture, 'retry'], check=True, timeout=10)
ok('deterministic 30 s total retry budget, immediate success, retry and saturation')
with tempfile.TemporaryDirectory(prefix='goat-cleanup-данные 😀-') as tmp:
    root = Path(tmp).resolve()
    owners = []
    try:
        lazy = Owner(root, 'lazy', 'lazy'); owners.append(lazy)
        assert lazy.line() == 'lazy'
        lazy.ended()
        assert not lazy.pid_file.exists() and not list(lazy.root.glob('goat-run-*'))
        ok('no process or directory without temporary work')

        for fault in ['missing', 'exit', 'silent', 'late']:
            owner = Owner(root, fault, 'missing' if fault == 'missing' else 'failure',
                          None if fault == 'missing' else fault)
            owners.append(owner)
            assert owner.line(timeout=4).startswith('failed ')
            assert not list(owner.root.glob('goat-run-*'))
            if fault in ('silent', 'late'):
                owner.pin()
            owner.send('ack')
            owner.ended()
            if owner.cleaner:
                owner.cleaner.wait()
        ok('creation/early exit/no READY/late READY: sticky shared failure, no native files')

        a = Owner(root, 'normal'); owners.append(a); a.ready()
        unrelated = a.root / 'goat-run-old'
        unrelated.mkdir(); (unrelated / 'saved.gbin').write_bytes(b'saved')
        saved = a.root / 'saved.so'; saved.write_bytes(b'published')
        a.send('short'); assert a.line() == 'ran 0 1'
        assert (a.directory / 'relative-output').exists()
        a.send('finish'); assert a.line() == 'finished 0'
        a.cleaned()
        assert saved.read_bytes() == b'published' and (unrelated / 'saved.gbin').read_bytes() == b'saved'
        a.send('quit'); a.ended()
        ok('eight first callers, managed result, explicit cleanup, preserved saved/old objects')

        b = Owner(root, 'parallel'); owners.append(b); b.ready()
        for command, code in [('quit', 0), ('exit', 23)]:
            a = Owner(root, "owner-" + command); owners.append(a); a.ready()
            a.send(command); a.ended(code); a.cleaned()
            assert b.directory.exists()
        ok('ordinary and explicit owner exit, isolated concurrent run')

        a = Owner(root, 'killed-owner'); owners.append(a); a.ready()
        a.process.kill(); a.process.wait(timeout=10); a.cleaned()
        assert b.directory.exists()
        ok('external owner termination triggers independent cleanup')

        # Caller policy is outside the service. A confirmed READY stays READY after cleaner death.
        for code in (0, 23):
            a = Owner(root, 'dead-cleaner-' + str(code)); owners.append(a); a.ready()
            a.cleaner.kill(); a.cleaner.wait()
            a.send('check'); assert a.line() == 'alive'
            a.send('short'); assert a.line() == 'ran 3 -1'
            a.send('quit' if code == 0 else 'exit'); a.ended(code)
            assert a.directory.exists()
        ok('dead cleaner does not revoke READY or change user exit; communication error is explicit')

        a = Owner(root, 'active-compiler'); owners.append(a); a.ready()
        a.send('run')
        workers = []
        for name in ('compiler', 'worker'):
            path = a.directory / name
            def read_worker(path=path):
                try:
                    text = path.read_text().strip()
                    return int(text) if text else None
                except FileNotFoundError:
                    return None
            workers.append(ProcessWatch(until(read_worker)))
        a.process.kill(); a.process.wait(timeout=10)
        for worker in workers:
            worker.wait(); worker.close()
        a.cleaned()
        assert b.directory.exists()
        ok('compiler and descendant stopped before directory cleanup, parallel owner unaffected')

        a = Owner(root, 'child-deadline'); owners.append(a); a.ready()
        a.send('timeout'); assert a.line() == 'ran 0 3'
        a.send('finish'); assert a.line() == 'finished 0'
        a.cleaned(); a.send('quit'); a.ended()
        ok('managed compiler timeout and completed tree shutdown')

        # A command racing with owner exit must never cancel or duplicate cleanup.
        for i in range(8):
            a = Owner(root, 'race-' + str(i)); owners.append(a); a.ready()
            a.send('finish'); a.process.kill(); a.process.wait(timeout=10); a.cleaned()
        ok('cleanup request/owner death/lost receiver race')

        for i in range(8):
            a = Owner(root, 'launch-race-' + str(i)); owners.append(a); a.ready()
            a.send('run'); a.process.kill(); a.process.wait(timeout=10); a.cleaned()
        ok('owner death racing compiler launch leaves no unregistered tree')

        if os.name == 'nt':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                           ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            a = Owner(root, 'busy'); owners.append(a); a.ready()
            handle = kernel.CreateFileW(str(a.directory / 'данные 😀.tmp'), 0x80000000,
                                        1, None, 3, 0, None)
            assert handle != ctypes.c_void_p(-1).value
            try:
                a.send('finish'); assert a.line(timeout=8) == 'finished 2'
                assert a.directory.exists()
                a.send('quit'); a.ended()
                # Continue the SAME cleanup after the caller's separate 5 s wait and exit.
            finally:
                kernel.CloseHandle(handle)
            a.cleaned()
            ok('5 s client timeout does not cancel 30 s cleanup; busy file released after owner exit')

        b.send('finish'); assert b.line() == 'finished 0'; b.cleaned(); b.send('quit'); b.ended()
    finally:
        for owner in owners:
            owner.close()
print(f'Cleanup service testing done; total: {passed}, passed: {passed}, failed: 0', flush=True)
