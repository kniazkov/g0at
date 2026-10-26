"""Explicit process identity and synchronization helpers for subprocess acceptance tests."""
import ctypes
import os
import select
import signal
import threading
import time

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

    def alive(self):
        if os.name == 'nt':
            return self.kernel.WaitForSingleObject(self.handle, 0) == 258
        return not select.select([self.handle], [], [], 0)[0]

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

