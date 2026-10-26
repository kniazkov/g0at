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


def musl_image(release="1.2.5", banner=True, duplicate=False):
    """Synthetic layout fixture, never evidence of native musl execution."""
    data = elf(note=False)
    # No symbol versions: musl deliberately has no GLIBC version namespace.
    struct.pack_into("<I", data, 64 + 3 * 64 + 4, 0)
    struct.pack_into("<Q", data, 32, 3328)
    struct.pack_into("<HH", data, 54, 56, 1)
    struct.pack_into("<IIQQQQQQ", data, 3328, 1, 4, 3456, 3456, 3456, 512, 512, 1)
    text = b"musl libc (x86_64)\nVersion %s\nDynamic Program Loader\n"
    if banner:
        data[3456:3456 + len(text)] = text
    encoded = release.encode() + b"\0"
    data[3584:3584 + len(encoded)] = encoded
    if duplicate:
        data[3680:3686] = b"1.2.4\0"
    return data


def musl_module(dependencies=("libc.so",)):
    data = elf(dependencies=dependencies, note=False)
    struct.pack_into("<I", data, 64 + 3 * 64 + 4, 0)
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
    if sys.platform.startswith('linux'):
        import resource
        def stack(mode, expected, limited=False):
            nonlocal count
            def lower_limit():
                resource.setrlimit(resource.RLIMIT_STACK, (128 * 1024, 128 * 1024))
            result = subprocess.run([executable, '--stack-' + mode], capture_output=True,
                                    text=True, timeout=10,
                                    preexec_fn=lower_limit if limited else None)
            assert result.returncode == 0 and result.stdout == str(expected) + '\n', result
            count += 1
        stack('main', 1)
        stack('worker', 1)
        stack('small', 0)
        stack('main', 0, limited=True)
        blocked = subprocess.run([executable, '--stack-blocked'], capture_output=True,
                                 text=True, timeout=10)
        assert blocked.returncode == 0 and blocked.stdout in ('0\n', 'not-musl\n'), blocked
        if blocked.stdout == '0\n':
            count += 1
    with tempfile.TemporaryDirectory(prefix="goat-image-requirements-") as directory:
        directory = pathlib.Path(directory)
        def check(data, succeeds=True, contains=None, target="glibc"):
            nonlocal count
            path = directory / "image.bin"
            path.write_bytes(data)
            run = subprocess.run([executable, str(path), str(directory), target],
                                 capture_output=True, text=True, timeout=10)
            assert (run.returncode == 0) == succeeds, (run.returncode, run.stderr)
            assert contains is None or all(value in run.stdout + run.stderr
                for value in ((contains,) if isinstance(contains, str) else contains)), (run.stdout, run.stderr)
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
        # Static parser fixtures complement actual musl compiler/runtime CI jobs.
        libc = directory / "libc.so"
        libc.write_bytes(musl_image())
        check(musl_module(), target="musl", contains="2.6.39.0 1.2.5.0")
        check(musl_module(), False, target="glibc")
        check(elf(), False, target="musl")
        check(musl_module(dependencies=()), False, target="musl")
        for contents in (musl_image(banner=False), musl_image(duplicate=True),
                         musl_image("1.2.5-dev"), musl_image("1.2"),
                         musl_image("1.2.4294967296"), musl_image("2.0.0")):
            libc.write_bytes(contents)
            check(musl_module(), False, target="musl")
        for offset, value in ((3328 + 8, 0xffffffffffffffff),
                              (3328 + 32, 0xffffffffffffffff),
                              (3328 + 40, 1)):
            data = musl_image(); struct.pack_into("<Q", data, offset, value)
            libc.write_bytes(data)
            check(musl_module(), False, target="musl")
        for flags in (0, 2, 6):
            data = musl_image(); struct.pack_into("<I", data, 3328 + 4, flags)
            libc.write_bytes(data)
            check(musl_module(), False, target="musl")
        data = musl_image(); struct.pack_into("<I", data, 3328 + 4, 5)
        libc.write_bytes(data)
        check(musl_module(), target="musl")
        arm_libc = musl_image()
        struct.pack_into("<H", arm_libc, 18, 183)
        start = arm_libc.index(b"x86_64")
        arm_libc[start:start + 6] = b"aarch64"
        # Keep segment/file boundaries intact after the longer architecture name.
        arm_libc.pop()
        libc.write_bytes(arm_libc)
        arm_module = musl_module(); struct.pack_into("<H", arm_module, 18, 183)
        check(arm_module, target="musl-aarch64", contains="3.7.0.0 1.2.5.0")
        check(musl_module(), False, target="musl")
        libc.write_bytes(elf())
        check(musl_module(), False, target="musl")
        libc.write_bytes(musl_image())
        check(elf(dependencies=("libc.so",)), False, target="musl")
        check(pe(), target="msvcrt", contains="10.0.0.0 0.0.0.0")
        data = pe(); data[1794:1801] = b"_iob\0\0\0"
        check(data, target="msvcrt")
        data = pe(); data[1794:1803] = b"__p__iob\0"
        check(data, target="msvcrt")
        for symbol in ("GetCPInfo", "GetStringTypeW", "MultiByteToWideChar", "WideCharToMultiByte"):
            data = pe(); encoded = symbol.encode() + b"\0"
            data[1858:1858 + len(encoded)] = encoded
            check(data, target="msvcrt")
        for symbol in ("_create_locale", "_free_locale", "_strtod_l", "_vsnprintf_l"):
            data = pe("ucrtbase.dll"); encoded = symbol.encode() + b"\0"
            data[1794:1794 + len(encoded)] = encoded
            check(data, target="ucrt")
        for symbol in ("__lc_codepage", "__p___mb_cur_max", "_setjmp3", "localeconv",
                       "longjmp", "strchr", "___lc_codepage_func", "___mb_cur_max_func", "_setjmp"):
            data = pe(); encoded = symbol.encode() + b"\0"
            data[1794:1794 + len(encoded)] = encoded
            check(data, target="msvcrt")
        for symbol in ("__intrinsic_setjmp", "longjmp", "strchr"):
            data = pe("api-ms-win-crt-private-l1-1-0.dll")
            encoded = symbol.encode() + b"\0"
            data[1794:1794 + len(encoded)] = encoded
            check(data, target="ucrt")
        data = pe(); data[1794:1801] = b"future\0"
        data[1858:1866] = b"Unknown\0"
        check(data, False, target="msvcrt", contains=("msvcrt.dll!future", "KERNEL32.dll!Unknown"))
        data = pe()
        # Many bounded import entries must fail with an explicit bounded inventory.
        for index in range(30):
            struct.pack_into("<Q", data, 1536 + index * 8, 0x1500)
        struct.pack_into("<Q", data, 1536 + 30 * 8, 0)
        data[1794:1995] = b"x" * 200 + b"\0"
        check(data, False, target="msvcrt", contains="[inventory truncated]")
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
