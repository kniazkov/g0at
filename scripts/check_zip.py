"""Cross-platform ZIP wrapper checks with independent Python zipfile fixtures."""
import hashlib
import io
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import warnings
import zipfile
import zlib

EXE = str(Path(sys.argv[1]).resolve())
ROOT = Path(__file__).resolve().parents[1]
COUNT = 0
OK, INVALID, MEMORY, FORMAT, ZIP64, MULTIDISK, UNSUPPORTED, CAPACITY, SIZE, CRC, DEFLATE, HEADER = range(12)


def run(mode, data=b'', args=(), status=OK):
    global COUNT
    p = subprocess.run([EXE, mode, *map(str, args)], input=data, capture_output=True, timeout=20)
    COUNT += 1
    allowed = status if isinstance(status, tuple) else (status,)
    assert p.returncode in allowed, (mode, args, p.returncode, allowed, p.stderr[:500])
    return p.stdout


def inspect(data):
    return json.loads(run('inspect', data))


def read(data, index, capacity, status=OK):
    return run('read', data, (index, capacity), status)


def write(entries, comment=b'', limit=32*1024*1024, status=OK):
    return run('write', json.dumps(dict(entries=entries, comment=comment.hex())).encode(), (limit,), status)


def entry(name, data, method=8, **kw):
    return dict(name=name.hex(), data=data.hex(), method=method, flags=0x800,
                date=33, time=0, madeBy=0x314, internal=0, external=0o100644 << 16, **kw)


def independent(parts, stream=False):
    class NonSeek(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation('non-seekable')
    out = NonSeek() if stream else io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(out, 'w') as archive:
            archive.comment = b'archive\0comment'
            for name, content, method in parts:
                zi = zipfile.ZipInfo(name, (2001, 2, 3, 4, 5, 6))
                zi.compress_type = method
                zi.extra = b'\xfe\xca\x03\x00abc'
                zi.comment = b'file\0comment'
                archive.writestr(zi, content)
    return out.getvalue()


def patch(data, offset, fmt, value):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


subprocess.run([EXE], check=True)
# Independent stored, Deflate, mixed, descriptors, binary/empty and duplicate names.
parts = [('same', b'', 0), ('same', b'abc\0\xff'*1000, 8),
         ('длинное-😀', bytes(range(256))*20, 0), ('empty-deflate', b'', 8)]
for streamed in (False, True):
    data = independent(parts, streamed)
    meta = inspect(data)
    assert meta['comment'] == b'archive\0comment'.hex()
    assert len(meta['entries']) == len(parts)
    for i, (name, content, method) in enumerate(parts):
        e = meta['entries'][i]
        assert bytes.fromhex(e['name']).decode() == name
        assert e['method'] == method and e['size'] == len(content)
        assert bytes.fromhex(e['extra']) == b'\xfe\xca\x03\x00abc'
        assert e['comment'] == b'file\0comment'.hex()
        assert e['flags'] & 8 == (8 if streamed else 0)
        assert read(data, i, len(content)) == content
        if content:
            read(data, i, len(content)-1, CAPACITY)
# Long names are not silently truncated by upstream convenience structures.
long_name = 'x'*60000
long_zip = independent([(long_name, b'x', 8)])
assert bytes.fromhex(inspect(long_zip)['entries'][0]['name']).decode() == long_name
assert read(long_zip, 0, 1) == b'x'
# Names with embedded NUL are exposed with original lengths, not C-string lengths.
data = independent([('abc', b'xyz', 0)])
meta = inspect(data); e = meta['entries'][0]
broken = bytearray(data)
broken[e['centralOffset']+47] = broken[e['localOffset']+31] = 0
assert inspect(bytes(broken))['entries'][0]['name'] == '610063'
assert read(bytes(broken), 0, 3) == b'xyz'
# Every truncation is rejected without crashing or exposing output.
short = independent([('x', b'12345'*10, 8)])
for n in range(len(short)):
    run('inspect', short[:n], status=(FORMAT, ZIP64, MULTIDISK))
# Malformed extra field lengths must fail before invoking upstream parsing.
e = inspect(data)['entries'][0]
extra_offset = e['centralOffset'] + 46 + 3
run('inspect', patch(data, extra_offset+2, '<H', 65535), status=FORMAT)
run('inspect', patch(data, extra_offset, '<H', 1), status=ZIP64)
# Local/central inconsistencies remain inspectable and fail extraction.
base = independent([('x', b'abcdef', 0)])
e = inspect(base)['entries'][0]; c, l, d = e['centralOffset'], e['localOffset'], e['dataOffset']
for offset, fmt, value in [(l+4, '<H', 10), (l+10, '<H', 42), (l+12, '<H', 42), (l+6, '<H', 1), (l+8, '<H', 8), (l+14, '<I', 123),
                           (l+18, '<I', 5), (l+22, '<I', 5), (l+30, '<B', ord('z'))]:
    bad = patch(base, offset, fmt, value)
    inspect(bad)
    read(bad, 0, 6, HEADER)
bad = patch(patch(base, c+16, '<I', 0), l+14, '<I', 0)
read(bad, 0, 6, CRC)
read(patch(base, d, '<B', 255), 0, 6, CRC)
# Encryption, unsupported method and symlink metadata never disappear.
for flag in (1, 0x40, 0x2000):
    bad = patch(base, c+8, '<H', flag)
    assert inspect(bad)['entries'][0]['flags'] == flag
    read(bad, 0, 6, UNSUPPORTED)
bad = patch(base, c+10, '<H', 99)
assert inspect(bad)['entries'][0]['method'] == 99
read(bad, 0, 6, UNSUPPORTED)
bad = patch(patch(base, c+4, '<H', 0x314), c+38, '<I', 0o120777 << 16)
assert inspect(bad)['entries'][0]['external'] == 0o120777 << 16
# Distinct unsupported format errors, including local ZIP64 data in extra fields.
eocd = base.rfind(b'PK\x05\x06')
for offset, fmt, value in [(eocd+10, '<H', 65535), (eocd+12, '<I', 0xffffffff),
                           (c+20, '<I', 0xffffffff), (c+42, '<I', 0xffffffff), (c+34, '<H', 65535)]:
    run('inspect', patch(base, offset, fmt, value), status=ZIP64)
for offset in (eocd+4, eocd+6, c+34):
    run('inspect', patch(base, offset, '<H', 1), status=MULTIDISK)
# SFX prefix/trailer and directory/path spellings remain available to #182.
sfx = b'prefix' + base + b'trailer'
meta = inspect(sfx)
assert meta['prefix'] == 6 and meta['trailing'] == 7
assert read(sfx, 0, 6) == b'abcdef'
paths = independent([('../evil', b'a', 0), ('/absolute', b'b', 8), ('dir/', b'', 0)])
assert [bytes.fromhex(e['name']).decode() for e in inspect(paths)['entries']] == ['../evil', '/absolute', 'dir/']
# Inflate must reach stream end, consume every compressed byte and match the size.
deflated = independent([('x', b'a'*10000, 8)])
e = inspect(deflated)['entries'][0]; c, l, d = e['centralOffset'], e['localOffset'], e['dataOffset']
read(patch(deflated, d, '<B', 7), 0, 10000, DEFLATE)
for size in (0, 1, 9999, 10001):
    bad = patch(patch(deflated, c+24, '<I', size), l+22, '<I', size)
    read(bad, 0, 10001, SIZE)
for packed in (0, 1, e['compressed']-1):
    bad = patch(patch(deflated, c+20, '<I', packed), l+18, '<I', packed)
    read(bad, 0, 10000, DEFLATE)
# Insert junk inside the declared compressed stream and adjust central placement.
bad = deflated[:c] + b'!' + deflated[c:]
bad = patch(patch(bad, c+1+20, '<I', e['compressed']+1), l+18, '<I', e['compressed']+1)
bad = patch(bad, bad.rfind(b'PK\x05\x06')+16, '<I', c+1)
read(bad, 0, 10000, DEFLATE)
# Writer parameters, binary comments, exact dates/flags/attrs and both methods.
rng = random.Random(247)
contents = [b'', b'x', b'xy', b'xyz', bytes(range(256)), b'abc'*10000,
            bytes(rng.randrange(256) for _ in range(65537))]
for method in (0, 8):
    entries = [entry(f'part-{i}'.encode(), content, method, comment=b'x\0y'.hex(),
                     localExtra=b'\xfe\xca\x01\x00L'.hex(), centralExtra=b'\xfe\xca\x01\x00C'.hex())
               for i, content in enumerate(contents)]
    encoded = write(entries, b'global\0comment')
    assert write(entries, b'global\0comment') == encoded
    meta = inspect(encoded)
    assert bytes.fromhex(meta['comment']) == b'global\0comment'
    with zipfile.ZipFile(io.BytesIO(encoded)) as archive:
        for i, content in enumerate(contents):
            assert archive.read(f'part-{i}') == content
            e = meta['entries'][i]
            assert e['method'] == method and e['flags'] == 0x800 and e['needed'] == 20
            assert e['date'] == 33 and e['time'] == 0 and e['internal'] == 0
            assert e['external'] == 0o100644 << 16 and e['madeBy'] == 0x314
            assert e['comment'] == b'x\0y'.hex()
            assert e['localExtra'] == b'\xfe\xca\x01\x00L'.hex()
            assert e['extra'] == b'\xfe\xca\x01\x00C'.hex()
            assert read(encoded, i, len(content)) == content
    assert write(entries, b'global\0comment', len(encoded)) == encoded
    write(entries, b'global\0comment', len(encoded)-1, CAPACITY)
# All timestamps and supported flags explicit, including DOS zero date.
opts = entry(b'name', b'text\n')
opts.update(time=0x1234, date=0, flags=0, internal=7, external=123, madeBy=0x714)
m = inspect(write([opts]))['entries'][0]
assert all(m[k] == opts[k] for k in ('time', 'date', 'flags', 'internal', 'external', 'madeBy'))
assert len(inspect(write([entry(b'x', b'a'), entry(b'x', b'b')]))['entries']) == 2
assert inspect(write([]))['entries'] == []
for limit in (0, 21, 22, 32, 50, 100):
    write([entry(b'x', b'x'*100)], limit=limit, status=CAPACITY)
write([entry(b'x\0y', b'')], status=INVALID)
write([entry(b'', b'')], status=INVALID)
write([entry(b'x', b'', 99)], status=UNSUPPORTED)
write([entry(b'x', b'', localExtra='00')], status=FORMAT)
write([entry(b'x', b'', centralExtra='01000000')], status=ZIP64)
# Rejected ZIP64 stays distinguishable even when only local headers need it.
forced = io.BytesIO()
with zipfile.ZipFile(forced, 'w') as archive:
    with archive.open('x', 'w', force_zip64=True) as member:
        member.write(b'abc')
info = inspect(forced.getvalue())['entries'][0]
assert bytes.fromhex(info['localExtra'])[:2] == b'\x01\x00'
read(forced.getvalue(), 0, 3, HEADER)
# Frozen profile 1 proves exact compression and envelope parity, not only roundtrip.
vectors = ROOT / 'docs/module-format-v1/vectors'
metadata = (vectors / 'empty.module.json').read_bytes().rstrip(b'\n')
bytecode = (vectors / 'empty.bytecode.bin').read_bytes()
encoded = write([entry(b'module.json', metadata), entry(b'bytecode.bin', bytecode)])
assert encoded == (vectors / 'empty.gbin').read_bytes(), hashlib.sha256(encoded).hexdigest()
# Malformed lengths/offsets and deterministic random damage are bounded failures.
e = inspect(base)['entries'][0]
for offset, fmt, value in [(e['centralOffset']+28, '<H', 65535), (e['centralOffset']+42, '<I', 0xfffffffe),
                           (eocd+12, '<I', 0xfffffffe), (eocd+16, '<I', 0xfffffffe)]:
    run('inspect', patch(base, offset, fmt, value), status=FORMAT)
for _ in range(60):
    damaged = bytearray(deflated)
    for _ in range(3):
        damaged[rng.randrange(len(damaged))] ^= 1 << rng.randrange(8)
    run('read', bytes(damaged), (0, 10000), tuple(range(12)))
print(f'ZIP checks passed: {COUNT}')
