#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Independent v1 producer, structural corruptions and actual reader/VM checks."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def fixture():
    strings = ['println', 'A\0Ж😀', 'x']
    payload, descriptors, instructions = bytearray(), bytearray(), bytearray()
    for text in strings:
        payload += bytes((-len(payload)) % 8)
        raw = text.encode('utf-32-le') + bytes(4)
        descriptors += struct.pack('<4I', len(payload), len(raw), 1, len(text))
        payload += raw
    payload += bytes((-len(payload)) % 8)
    descriptors += struct.pack('<4I', len(payload), 4, 2, 1)
    payload += struct.pack('<I', 2)
    def emit(op, b=0, a=0):
        instructions.extend(struct.pack('<BBHI', op, 0, a, b))
    def print_top():
        emit(15, 0); emit(48, a=1); emit(5)
    for bits, op in [(0x8000000000000000, 11), (0x7fffffffffffffff, 11),
                     (0x8000000000000000, 12), (0x7ff0000000000000, 12),
                     (0x7ff8000000000042, 12)]:
        emit(1, bits & 0xffffffff); emit(op, bits >> 32); print_top()
    emit(13, 1); print_top()
    # A retained one-argument function, after which the top level ends.
    creation = len(instructions)//8 + 1
    body = creation + 3
    emit(1, body); emit(47, 3, 1); emit(5); emit(2)
    emit(7); emit(50)
    ni, nd = len(instructions)//8, 4
    od = 64 + ni*8; of = od+nd*16; on = of+16; ol = on+32; start = ol+48
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start+len(payload),
                         ni, nd, 1, 1, 2, 64, od, of, on, ol, start)
    return (header + instructions + descriptors + struct.pack('<4I', 0, ni, 0, 0)
            + struct.pack('<8I', 7, 0, creation, body, 3, 1, 0xffffffff, 0)
            + struct.pack('<6I', 0, body, 0, 1, 1, 0)
            + struct.pack('<6I', body, 2, 0, 2, 1, 7) + payload)

def main():
    goat, work = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    path = work/'image.gbin'
    def run(data, valid=True):
        path.write_bytes(data)
        result = subprocess.run([str(goat), '--run', '--native', 'off', str(path)],
                                capture_output=True, timeout=20)
        assert (result.returncode == 0) == valid, (result.returncode, result.stderr, data[:64].hex())
        if not valid:
            assert b'Invalid, incompatible or unreadable Goat binary' in result.stderr
            assert result.stdout == b'', 'invalid image executed user code before rejection'
        assert path.read_bytes() == data
        return result.stdout.replace(b'\r\n', b'\n')
    image = bytes.fromhex((ROOT/'test/data/binary-v1/scalars.hex').read_text())
    assert image == fixture()
    expected = '-9223372036854775808\n9223372036854775807\n-0\ninf\nnan\nA\0Ж😀\n'.encode()
    assert run(image) == expected
    minimal = bytes.fromhex((ROOT/'docs/module-format-v1/vectors/empty.bytecode.hex').read_text())
    assert run(minimal) == b''
    h = struct.unpack_from('<8s14I', image)
    ni, nd, of, on, ol, payload = h[4], h[5], h[11], h[12], h[13], h[14]
    od = h[10]
    corruptions = 0
    def bad(offset, value, fmt='<I'):
        nonlocal corruptions
        data = bytearray(image); struct.pack_into(fmt, data, offset, value)
        run(data, False); corruptions += 1
    for magic in [b'GOATBIN1', b'GOATBI10', b'GOATBI11', b'goat v.1', b'GOATSC01', b'GOATST01']:
        run(magic+image[8:], False); corruptions += 1
    for offset in range(8, 64, 4):
        bad(offset, 0xffffffff)
    for offset in [0, 7, 31, 63, 64, len(image)-1]:
        run(image[:offset], False); corruptions += 1
    run(image+b'\0', False); corruptions += 1
    bad(64, 255, '<B'); bad(65, 1, '<B'); bad(66, 1, '<H')
    bad(64, 3, '<B'); bad(of+8, 1)  # A jump to an ARG consumer.
    bad(od, 1); bad(od+4, 0xffffffff); bad(od+8, 0); bad(od+12, 0xffffffff)
    bad(od+16, 0)  # Aliased descriptors.
    bad(payload, 0xd800); bad(payload, 0x110000)
    bad(payload+7*4, 1)  # Terminator of println.
    parameter_offset = struct.unpack_from('<I', image, od+3*16)[0]
    bad(payload+parameter_offset, 3)  # Parameter vector points to itself.
    for offset in [of, of+4, of+8, of+12, on, on+4, on+8, on+12,
                   on+16, on+20, on+28, ol, ol+4, ol+8, ol+20]:
        bad(offset, 0xffffffff)
    bad(ol+12, 0)  # Mixed synthetic and real line/column.
    bad(on+24, 3)  # A name must reference a string.
    bad(on+8, 1)  # Function table points to a real constant.
    bad(on+12, 1)  # Function body is an ARG consumer.
    bad(ol+24+20, 8)  # Missing function ID.
    body = struct.unpack_from('<I', image, on+12)[0]
    bad(of+8, body)  # File entry may not start inside a function.
    bad(ol+24+20, 0)  # The body must retain its declared function identity.
    # Sparse IDs cover the entire uint32 range, not the function-array index.
    sparse = bytearray(image)
    struct.pack_into('<I', sparse, on, 0xffffffff)
    struct.pack_into('<I', sparse, ol+24+20, 0xffffffff)
    assert run(sparse) == expected
    sys.path.insert(0, str(ROOT/'docs/module-format-v1'))
    spec = importlib.util.spec_from_file_location('format_reference', ROOT/'docs/module-format-v1/reference.py')
    ref = importlib.util.module_from_spec(spec); spec.loader.exec_module(ref)
    ref.check_bytecode(image)
    # The C writer must produce this exact independently specified empty image.
    empty_source = work/'empty.goat'
    empty_source.write_text('', encoding='utf-8')
    subprocess.run([str(goat), '--compile', str(empty_source)], check=True)
    empty_artifact = empty_source.with_suffix('.gbin').read_bytes()
    expected_empty = bytearray(minimal)
    struct.pack_into('<II', expected_empty, 100, 0, 0)
    assert empty_artifact[96:] == expected_empty
    source = work/'roundtrip.goat'
    source.write_text('const outer=func(n){const inner=func(x){return x+1;};return inner(n);};'
                      'const unused=func(){return 99;};println(outer(41));', encoding='utf-8')
    for opt in ['none', 'all']:
        subprocess.run([str(goat), '--compile', '--optimize', opt, str(source)], check=True)
        artifact = source.with_suffix('.gbin').read_bytes()
        assert artifact[:8] == b'GOATPG01' and struct.unpack_from('<Q', artifact, 16)[0] == 0
        code_size = struct.unpack_from('<Q', artifact, 24)[0]
        code = artifact[96:96+code_size]
        ref.check_bytecode(code)
        assert run(code) == b'42\n'
    print(f'[ ok ] Bytecode v1: independent numeric/string/function image, {corruptions} corruptions, shared codec')

if __name__ == '__main__':
    if sys.argv[1:] == ['--generate']:
        path = ROOT/'test/data/binary-v1/scalars.hex'
        path.parent.mkdir(exist_ok=True)
        path.write_text(fixture().hex()+'\n')
    else:
        main()
