#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Independent UTF-32LE fixture, corruptions, VM/folding/native and explicit NULs."""
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

def fixture():
    # A complete independent legacy-container transition fixture. #251 replaces
    # the non-string sections; this test deliberately pins the #250 signature.
    strings = ['', 'ASCII', 'Привет', '😀', 'e\u0301', '\0A\0B\0\0']
    payload = bytearray()
    descriptors = bytearray()
    instructions = bytearray()
    for index, text in enumerate(strings):
        encoded = text.encode('utf-32-le') + bytes(4)
        descriptors += struct.pack('<QI', len(payload), len(encoded))
        payload += encoded
        instructions += struct.pack('<BBHI', 13, 0, 0, index)  # SLOAD
        instructions += struct.pack('<BBHI', 5, 0, 0, 0)      # POP
    instructions += struct.pack('<BBHI', 2, 0, 0, 0)          # END
    inner = struct.pack('<8sQQQ', b'GOATSC01', 32, 32 + len(instructions),
                        32 + len(instructions) + len(descriptors))
    inner += instructions + descriptors + payload
    outer = bytearray(96)
    struct.pack_into('<8sQQQQ', outer, 0, b'GOATST01', 1, 0x104, len(inner), 0)
    outer += inner
    struct.pack_into('<Q', outer, 56, checksum(outer))
    return outer

def main():
    goat, work = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    def run(*args, ok=True, locale='C'):
        result = subprocess.run([str(goat), *map(str, args)], capture_output=True,
                                env=dict(os.environ, GOAT_LANGUAGE='en', LC_ALL=locale), timeout=60)
        assert (result.returncode == 0) == ok, (args, result.returncode, result.stderr)
        return result.stdout.replace(b'\r\n', b'\n')
    image = bytes.fromhex((ROOT/'test/unicode/scalars.gbin.hex').read_text())
    assert image == fixture()
    path = work/'scalars.gbin'
    path.write_bytes(image)
    run('--run', path)
    for magic in [b'GOATBI11', b'GOATBIN1', b'goat v.1']:
        damaged = bytearray(image); damaged[:8] = magic
        damaged[56:64] = bytes(8)
        struct.pack_into('<Q', damaged, 56, checksum(damaged))
        path.write_bytes(damaged)
        run('--run', path, ok=False)
    source = work/'unicode.goat'
    source.write_text('''const f=func(s){return s.toUpperCase();};
const g=func(s){return s.toLowerCase();};
println("😀".length);
println("A\\0B".length);
println("A" == "A\\0B");
println("A\\0B".indexOf("B"));
println(f("ß\\0я𐐨"));
println(g("ΟΣ ΟΣΑ ΟΣ́\\0Σ İ"));
println("A\\0B".replace("B", "Z"));
println("%0\\0%1".format("A\\0B", "C"));
''', encoding='utf-8')
    expected = '1\n3\nfalse\n2\nSS\0Я𐐀\nος οσα ος́\0σ i̇\nA\0Z\nA\0B\0C\n'.encode()
    for locale in ('C', 'C.UTF-8', 'tr_TR.UTF-8'):
        for opt in ('none', 'all'):
            assert run('--optimize', opt, source, locale=locale) == expected
            run('--compile', '--optimize', opt, source, locale=locale)
            assert run('--run', source.with_suffix('.gbin'), locale=locale) == expected
            regenerated = work/'regenerated.goat'
            regenerated.write_bytes(run('--compile', '--print-source-code', source))
            assert run(regenerated) == expected
        report = work/'native.report'
        assert run('--native', 'required', '--save-native', report, source, locale=locale) == expected
        fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
        assert int(fields['succeeded']) >= 2 and fields['retries'] == '0', fields
    raw = work/'raw-nul.goat'
    raw.write_bytes(b'println("A\x00B".length); println("http://a/*b*/");')
    assert run(raw) == b'3\nhttp://a/*b*/\n'
    raw.write_bytes(b'println(1);\x00println(2);')
    run(raw, ok=False)
    # Compile-time casing and runtime native casing must agree with fixed results.
    print('[ ok ] Unicode: independent fixture, legacy rejection, NULs, VM/folding/native, locales')

if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--generate':
        target = ROOT/'test/unicode/scalars.gbin.hex'
        target.parent.mkdir(exist_ok=True)
        target.write_text(fixture().hex()+'\n')
    else:
        main()
