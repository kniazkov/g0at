#!/usr/bin/env python3
"""Bounded native-image inspection, without loading executable code."""
import pathlib
import struct
import subprocess
import sys
import tempfile


def elf(requirement="GLIBC_2.9", dependencies=(), machine=62, note=True):
    data = bytearray(4096)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, machine, 1)
    struct.pack_into("<Q", data, 40, 64)
    struct.pack_into("<HHH", data, 58, 64, 5, 0)
    strings = bytearray(b"\0")
    def string(value):
        index = len(strings)
        strings.extend(value.encode() + b"\0")
        return index
    depnames = [string(value) for value in dependencies]
    version = string(requirement)
    def section(index, kind, offset, size, link=0):
        struct.pack_into("<IIQQQQIIQQ", data, 64 + index * 64,
                         0, kind, 0, 0, offset, size, link, 0, 1, 0)
    section(1, 3, 512, len(strings))
    data[512:512 + len(strings)] = strings
    section(2, 6, 1024, 16 * (len(dependencies) + 1), 1)
    for index, name in enumerate(depnames):
        struct.pack_into("<QQ", data, 1024 + index * 16, 1, name)
    section(3, 0x6ffffffe, 2048, 32, 1)
    struct.pack_into("<HHIII", data, 2048, 1, 1, 0, 16, 0)
    struct.pack_into("<IHHII", data, 2064, 0, 0, 0, version, 0)
    if note:
        section(4, 7, 3072, 32)
        struct.pack_into("<III", data, 3072, 4, 16, 1)
        data[3084:3088] = b"GNU\0"
        struct.pack_into("<IIII", data, 3088, 0, 3, 2, 0)
    return data


def pe(crt="msvcrt.dll", extra="KERNEL32.dll", machine=0x8664, os_major=10):
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, machine, 1)
    struct.pack_into("<HH", data, 148, 240, 0x2000)
    optional = 152
    struct.pack_into("<H", data, optional, 0x20b)
    struct.pack_into("<HH", data, optional + 40, os_major, 0)
    struct.pack_into("<HH", data, optional + 48, 6, 0)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + 8, 0x1000, 60)
    section = optional + 240
    struct.pack_into("<IIII", data, section + 8, 2048, 0x1000, 2048, 512)
    for index, name in enumerate((crt, extra)):
        struct.pack_into("<I", data, 512 + index * 20 + 12, 0x1100 + index * 128)
        start = 768 + index * 128
        data[start:start + len(name) + 1] = name.encode() + b"\0"
        struct.pack_into("<I", data, 512 + index * 20, 0x1400 + index * 32)
        struct.pack_into("<Q", data, 1536 + index * 32, 0x1500 + index * 64)
        function = b"malloc" if index == 0 else b"GetLastError"
        start = 1792 + index * 64 + 2
        data[start:start + len(function) + 1] = function + b"\0"
    return data


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="goat-image-requirements-") as directory:
        directory = pathlib.Path(directory)
        def check(data, succeeds=True, contains=None, target="glibc"):
            nonlocal count
            path = directory / "image.bin"
            path.write_bytes(data)
            run = subprocess.run([executable, str(path), str(directory), target],
                                 capture_output=True, text=True, timeout=10)
            assert (run.returncode == 0) == succeeds, (run.returncode, run.stderr)
            assert contains is None or contains in run.stdout + run.stderr, (run.stdout, run.stderr)
            count += 1
        check(elf(), contains="3.2.0.0 2.9.0.0")
        (directory / "dependency.so").write_bytes(elf("GLIBC_2.28"))
        check(elf(dependencies=("dependency.so",)), contains="2.28.0.0")
        (directory / "dependency.so").write_bytes(elf("GLIBC_2.40"))
        check(elf(dependencies=("dependency.so",)), contains="2.40.0.0")
        check(elf("GLIBC_ABI_DT_RELR"), contains="2.36.0.0")
        check(elf("GLIBC_FUTURE"), False)
        check(elf("GLIBC_PRIVATE"), False)
        check(elf("GCC_4.2.0"), False)
        check(elf(dependencies=("missing.so",)), False)
        check(elf(machine=183), False)
        check(elf(note=False), False)
        check(elf()[:30], False)
        data = elf(); struct.pack_into("<Q", data, 40, 0xffffffffffffffff)
        check(data, False)
        data = elf(); struct.pack_into("<I", data, 2056, 0xffffffff)
        check(data, False)
        data = elf(); data[5] = 2
        check(data, False)
        check(pe(), target="msvcrt", contains="10.0.0.0 0.0.0.0")
        data = pe(); data[1794:1801] = b"_iob\0\0\0"
        check(data, target="msvcrt")
        check(pe("ucrtbase.dll"), target="ucrt")
        check(pe("api-ms-win-crt-runtime-l1-1-0.dll"), target="ucrt")
        check(pe(), False, target="ucrt")
        check(pe(extra="unknown.dll"), False, target="msvcrt")
        check(pe(machine=0x14c), False, target="msvcrt")
        check(pe(os_major=11), target="msvcrt", contains="11.0.0.0")
        data = pe(); struct.pack_into("<I", data, 152 + 112 + 13 * 8, 0x1000)
        check(data, False, target="msvcrt")
        data = pe(); data[1794:1801] = b"future\0"
        check(data, False, target="msvcrt", contains="msvcrt.dll!future")
        data = pe(); struct.pack_into("<Q", data, 1536, 0x8000000000000001)
        check(data, False, target="msvcrt")
        check(b"not executable", False)
    print(f"Native requirements: {count}/{count} passed")


if __name__ == "__main__":
    main()
