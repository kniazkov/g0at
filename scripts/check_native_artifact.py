#!/usr/bin/env python3
"""Inspect ELF/PE exports without loading the produced or synthetic libraries."""
import json
import copy
import pathlib
import struct
import subprocess
import sys
import tempfile

DESCRIPTOR = b"GOATND01" + struct.pack("<Q", 2) + b"{}\0"


def elf(descriptor=DESCRIPTOR, adapter="adapter"):
    data = bytearray(4096)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", data, 16, 3, 62, 1)
    struct.pack_into("<Q", data, 40, 64)
    struct.pack_into("<HHH", data, 58, 64, 6, 0)
    def section(index, kind, flags, address, offset, size, link=0, stride=0):
        struct.pack_into("<IIQQQQIIQQ", data, 64 + index * 64,
                         0, kind, flags, address, offset, size, link, 0, 1, stride)
    names = b"\0goat_module_descriptor_v1\0" + adapter.encode() + b"\0"
    section(1, 3, 2, 0x1000, 512, len(names))
    data[512:512 + len(names)] = names
    section(2, 11, 2, 0x1100, 1024, 72, 1, 24)
    section(3, 1, 2, 0x2000, 2048, len(descriptor))
    section(4, 1, 6, 0x3000, 3072, 16)
    struct.pack_into("<IBBHQQ", data, 1048, 1, 0x11, 0, 3, 0x2000, len(descriptor))
    struct.pack_into("<IBBHQQ", data, 1072, 27, 0x12, 0, 4, 0x3000, 1)
    # Name offset follows the descriptor's terminating NUL.
    struct.pack_into("<I", data, 1072, names.index(adapter.encode()))
    data[2048:2048 + len(descriptor)] = descriptor
    data[3072] = 0xc3
    return data


def pe(wide=True, descriptor=DESCRIPTOR):
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, 0x8664 if wide else 0x14c, 2)
    length = 240 if wide else 224
    struct.pack_into("<HH", data, 148, length, 0x2000)
    optional = 152
    struct.pack_into("<H", data, optional, 0x20b if wide else 0x10b)
    struct.pack_into("<I", data, optional + (108 if wide else 92), 16)
    struct.pack_into("<II", data, optional + (112 if wide else 96), 0x1000, 256)
    table = optional + length
    struct.pack_into("<IIII", data, table + 8, 2048, 0x1000, 2048, 512)
    struct.pack_into("<I", data, table + 36, 0x40000040)
    struct.pack_into("<IIII", data, table + 48, 512, 0x2000, 512, 2560)
    struct.pack_into("<I", data, table + 76, 0x60000020)
    struct.pack_into("<IIIII", data, 512 + 20, 2, 2, 0x1040, 0x1050, 0x1060)
    struct.pack_into("<II", data, 576, 0x1300, 0x2000)
    struct.pack_into("<II", data, 592, 0x1070, 0x10a0)
    struct.pack_into("<HH", data, 608, 0, 1)
    name = b"goat_module_descriptor_v1\0"
    data[624:624 + len(name)] = name
    data[672:680] = b"adapter\0"
    data[1280:1280 + len(descriptor)] = descriptor
    data[2560] = 0xc3
    return data


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="goat-native-exports-") as folder:
        folder = pathlib.Path(folder)
        def check(data, succeeds=True, symbol="adapter"):
            nonlocal count
            path = folder / "image.bin"
            path.write_bytes(data)
            result = subprocess.run([executable, str(path), symbol], capture_output=True,
                                    text=True, timeout=10)
            assert (result.returncode == 0) == succeeds, (result.stdout, result.stderr)
            count += 1
        check(elf())
        check(elf(), False, "missing")
        check(elf(), False, "goat_module_descriptor_v1")
        data = elf(); data[1052] = 0x10
        check(data, False)
        data = elf(); data[1053] = 2
        check(data, False)
        data = elf(); struct.pack_into("<H", data, 1054, 0)
        check(data, False)
        data = elf(); struct.pack_into("<Q", data, 1064, 0xffffffffffffffff)
        check(data, False)
        data = elf(); struct.pack_into("<Q", data, 2056, 0xffffffffffffffff)
        check(data, False)
        data = elf(); data[2066] = 1
        check(data, False)
        data = elf(); struct.pack_into("<Q", data, 64 + 4 * 64 + 8, 2)
        check(data, False)
        check(pe())
        check(pe(False))
        check(pe(), False, "missing")
        data = pe(); struct.pack_into("<I", data, 580, 0x1080)
        check(data, False)
        data = pe(); struct.pack_into("<I", data, 576, 0x10a0)
        check(data, False)
        data = pe(); struct.pack_into("<I", data, 580, 0x1300)
        check(data, False)
        data = pe(); struct.pack_into("<H", data, 610, 2)
        check(data, False)
        data = pe(); struct.pack_into("<Q", data, 1288, 0xffffffffffffffff)
        check(data, False)
        check(b"not an image", False)
        vectors = pathlib.Path(__file__).resolve().parents[1] / "docs/module-format-v1/vectors"
        description = json.loads((vectors / "native-description.json").read_text())
        canonical = json.dumps(description, sort_keys=True, separators=(",", ":")).encode()
        envelope = b"GOATND01" + struct.pack("<Q", len(canonical)) + canonical + b"\0"
        metadata = json.loads((vectors / "empty.module.json").read_text())
        metadata.update(name=description["module"], version=description["version"],
                        mainHash=description["mainHash"])
        variant = {key: description[key] for key in ("goatAbiVersion", "target", "specializations")}
        variant.update(library="native/test.so", hash="0" * 64)
        metadata["native"] = [variant]
        metadata["functions"] = [dict(functionId=1, file=0, instruction=1, parameters=[dict(name="a"), dict(name="b")])]
        def validate(meta, image, expected):
            nonlocal count
            path = folder / "checked.bin"
            path.write_bytes(image)
            config = folder / "metadata.json"
            config.write_text(json.dumps(meta))
            run = subprocess.run([executable, "--validate", str(path), str(config)],
                                 capture_output=True, text=True, timeout=10)
            assert (run.returncode == 0) == expected, (run.stdout, run.stderr)
            count += 1
        image = elf(envelope, "goat_fn_1_v1")
        validate(metadata, image, True)
        changed = copy.deepcopy(metadata); changed["mainHash"] = "2" * 64
        validate(changed, image, False)
        changed = copy.deepcopy(metadata); changed["native"][0]["target"]["minimumOs"] = [6, 0]
        validate(changed, image, False)
        changed = copy.deepcopy(metadata)
        changed["native"][0]["specializations"][0]["specializationId"] = 2
        validate(changed, image, False)
        validate(metadata, elf(envelope, "wrong_adapter"), False)
        if len(sys.argv) > 2:
            source = folder / "fixture.c"
            library = folder / ("fixture.dll" if sys.platform == "win32" else "fixture.so")
            values = ",".join(str(value) for value in DESCRIPTOR)
            source.write_text(
                '#ifdef _WIN32\n#define EXPORT __declspec(dllexport)\n'
                '#else\n#define EXPORT __attribute__((visibility("default")))\n#endif\n'
                f'EXPORT const unsigned char goat_module_descriptor_v1[] = {{{values}}};\n'
                'EXPORT int adapter(void) { return 42; }\n'
                '#include <stdlib.h>\n'
                '__attribute__((constructor)) static void must_not_run(void) { abort(); }\n',
                encoding="ascii")
            subprocess.run([sys.argv[2], "-shared", "-fPIC", str(source), "-o", str(library)],
                           check=True, timeout=30)
            result = subprocess.run([executable, str(library), "adapter"],
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 0, (result.stdout, result.stderr)
            count += 1
    print(f"Native artifact exports: {count}/{count} passed")


if __name__ == "__main__":
    main()
