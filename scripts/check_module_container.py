"""Strict .gbin validation using independently built and deliberately damaged ZIPs."""
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = ROOT / 'test/data/python-samples'
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
BYTECODE = (VECTORS / 'empty.bytecode.bin').read_bytes()
OK, INVALID, MEMORY, LIMIT, ZIP, LAYOUT, PATH, DUPLICATE, TYPE, REQUIRED, UNDECLARED, JSON, MAIN_HASH, NATIVE_HASH = range(14)
COUNT = 0


def check(data, expected=OK, limit=None, zip_error=None):
    global COUNT
    args = [EXE, 'validate'] + ([] if limit is None else [str(limit)])
    p = subprocess.run(args, input=data, capture_output=True, timeout=20)
    COUNT += 1
    allowed = expected if isinstance(expected, tuple) else (expected,)
    assert p.returncode in allowed, (p.returncode, allowed, p.stderr[:1000])
    if zip_error is not None:
        assert f'zip={zip_error};'.encode() in p.stderr, p.stderr
    if p.returncode:
        assert not p.stdout, p.stdout
        return
    return json.loads(p.stdout)


def pack(parts, stream=False, extra=b'', comment=b'', attributes=None):
    class NonSeek(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation('stream')
    output = NonSeek() if stream else io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(output, 'w') as z:
            z.comment = comment
            for name, data, method in parts:
                info = zipfile.ZipInfo(name, (2001, 2, 3, 4, 5, 6))
                info.compress_type = method
                info.create_system = 3
                info.external_attr = (0o40755 << 16 | 0x10) if name.endswith('/') else 0o100640 << 16
                if attributes and name in attributes:
                    info.external_attr = attributes[name]
                info.extra = extra
                info.comment = b'entry\0comment'
                z.writestr(info, data)
    return output.getvalue()


def parts(metadata=BASE, bytecode=BYTECODE, native=(), method=8):
    return [('module.json', json.dumps(metadata, ensure_ascii=False, indent=2).encode(), method),
            ('bytecode.bin', bytecode, method), *native]


def patch(data, offset, fmt, value):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


def entries(data):
    end = data.rfind(b'PK\x05\x06')
    count, cd_size, cd = struct.unpack_from('<HII', data, end+10)
    result = []
    pos = cd
    for _ in range(count):
        name, extra, note = struct.unpack_from('<HHH', data, pos+28)
        local = struct.unpack_from('<I', data, pos+42)[0]
        n, e = struct.unpack_from('<HH', data, local+26)
        compressed = struct.unpack_from('<I', data, pos+20)[0]
        result.append(dict(c=pos, l=local, d=local+30+n+e, packed=compressed,
                           length=46+name+extra+note))
        pos += 46+name+extra+note
    assert pos == cd+cd_size
    return result


def no_descriptor_signatures(data):
    records = entries(data)
    out, central = bytearray(), bytearray()
    for r in records:
        offset = len(out)
        end = r['d']+r['packed']
        assert data[end:end+4] == b'PK\x07\x08'
        out += data[r['l']:end] + data[end+4:end+16]
        c = bytearray(data[r['c']:r['c']+r['length']])
        struct.pack_into('<I', c, 42, offset)
        central += c
    eocd = bytearray(data[data.rfind(b'PK\x05\x06'):])
    struct.pack_into('<II', eocd, 12, len(central), len(out))
    return bytes(out+central+eocd)


subprocess.run([EXE], check=True)
assert check((VECTORS / 'empty.gbin').read_bytes())['bytecode'] == BYTECODE.hex()
# Each method, mixed order, timestamps, comments, safe extras and optional directory.
for method in (0, 8):
    for streaming in (False, True):
        for extra in (b'', b'\x55\x54\x05\x00\x01\0\0\0\0', b'\xfe\xca\x03\x00abc'):
            data = pack(parts(method=method), streaming, extra, b'archive\0comment')
            assert check(data)['metadata'] == BASE
            if streaming:
                assert check(no_descriptor_signatures(data))['bytecode'] == BYTECODE.hex()
for method in (0, 8):
    data = pack([('native/', b'', method), *reversed(parts(method=method))])
    assert check(data)['native'] == []
check(pack([parts()[0], ('bytecode.bin', BYTECODE, 0)]))
# Snapshot lifetime is tested in C by overwriting/freeing the ZIP before access.
for n in (0, 1, 2, 3, 1024):
    payload = bytes(i % 256 for i in range(n))
    metadata = dict(BASE, mainHash=ref.main_hash(BASE, payload))
    assert check(pack(parts(metadata, payload)))['bytecode'] == payload.hex()
# All native variants are checked regardless of this host; payload format is #208/#233.
descriptor = json.loads((VECTORS / 'native-description.json').read_text())
variant = dict(library='native/variant-1.so', hash=hashlib.sha256(b'foreign-one').hexdigest(),
               goatAbiVersion=1, target=descriptor['target'], specializations=descriptor['specializations'])
other = copy.deepcopy(variant)
other.update(library='native/variant-2.dll', hash=hashlib.sha256(b'foreign-two').hexdigest())
other['target'].update(os='windows', minimumOs=[10], architecture='x86', bits=32,
                       runtime=dict(family='ucrt', minimumVersion=[10]))
metadata = dict(BASE, functions=[dict(functionId=1, file=0, instruction=0,
                                    parameters=[dict(name='a'), dict(name='b')])], native=[variant, other])
metadata['mainHash'] = ref.main_hash(metadata, BYTECODE)
native = [('native/variant-1.so', b'foreign-one', 0), ('native/variant-2.dll', b'foreign-two', 8)]
data = pack(parts(metadata, native=native))
assert {p['name']: bytes.fromhex(p['data']) for p in check(data)['native']} == {n: d for n, d, _ in native}
for i in range(2):
    bad = list(native); n, content, method = bad[i]; bad[i] = (n, content+b'!', method)
    check(pack(parts(metadata, native=bad)), NATIVE_HASH)
    bad_metadata = copy.deepcopy(metadata); bad_metadata['native'][i]['hash'] = '0'*64
    check(pack(parts(bad_metadata, native=native)), NATIVE_HASH)
    check(pack(parts(metadata, native=native[:i]+native[i+1:])), UNDECLARED)
check(pack(parts(native=native[:1])), UNDECLARED)
bad = copy.deepcopy(metadata); bad['native'][0]['library'] = 'native/missing.so'
check(pack(parts(bad, native=native)), UNDECLARED)
# Exact expanded limit and sums are checked before allocation/decompression.
expanded = sum(len(content) for _, content, _ in parts(metadata, native=native))
check(data, limit=expanded)
check(data, LIMIT, limit=expanded-1)
check(data, LIMIT, limit=0)
# Sum overflow/budget checks without gigabyte allocations, on both process bitnesses.
huge = pack(parts(method=0))
for r in entries(huge):
    huge = patch(patch(huge, r['c']+24, '<I', 0x90000000), r['l']+22, '<I', 0x90000000)
check(huge, LIMIT, limit=0xffffffff)
# Empty/missing mandatory parts and metadata parsing/hash failures.
check(pack([]), REQUIRED)
for only in parts(): check(pack([only]), REQUIRED)
check(pack(parts(dict(BASE, extra=True))), JSON)
check(pack(parts(dict(BASE, mainHash='0'*64))), MAIN_HASH)
check(pack(parts(bytecode=BYTECODE+b'!')), MAIN_HASH)
check(pack([('module.json', b'{"x":1,"x":2}', 8), parts()[1]]), JSON)
# Paths, duplicate records (including case-fold collisions), undeclared entries.
for path in ('../x', '/module.json', 'C:/x', 'native\\x.so', 'native/../x.so',
             'native//x.so', './module.json', 'native/a/b.so', 'native/x.so/',
             'native/x:ads.so', 'native/x .so', 'native/con.so', 'native/lpt9.dll',
             'module.json\0evil', 'README', 'Native/x.so', 'native/😀.so'):
    # ZipInfo sanitizes NUL on every host and backslash on Windows. Build a
    # safe placeholder, then put the exact forbidden bytes into both headers.
    name = path.encode('utf-8')
    base = bytearray(pack([*parts(), ('x' * len(name), b'', 0)]))
    r = entries(base)[-1]
    for offset in (r['l'] + 30, r['c'] + 46):
        base[offset:offset + len(name)] = name
        assert base[offset:offset + len(name)] == name
    check(bytes(base), PATH)
for path in ('module.json', 'Module.json', 'MODULE.JSON', 'bytecode.bin', 'ByteCode.Bin'):
    check(pack([*parts(), (path, b'', 0)]), DUPLICATE)
check(pack([*parts(), ('native/', b'', 0), ('native/', b'', 0)]), DUPLICATE)
check(pack([*parts(), ('native/', b'data', 0)]), TYPE)
# Unix symlink/socket/device/FIFO and DOS directory/reparse/device/volume flags.
for attrs in [kind << 16 for kind in (0o120777, 0o020600, 0o060600, 0o010600, 0o140600, 0o040755)] + [8, 16, 64, 0x400]:
    check(pack(parts(), attributes={'bytecode.bin': attrs}), TYPE)
check(pack([*parts(), ('native/', b'', 0)], attributes={'native/': 0o100644 << 16}), TYPE)
for extra in (b'\x01\0\0\0', b'\x75\x70\0\0', b'\x17\0\0\0', b'\x01\x99\0\0',
              b'\x0d\0\x0d\0'+b'\0'*13, b'\x6e\x75\x0e\0'+b'\0'*4+struct.pack('<H',0o120777)+b'\0'*8):
    check(pack(parts(), extra=extra), ZIP)
# Inconsistent fields remain invalid before hashes, including descriptor placeholders.
for streamed in (False, True):
    base = pack(parts(method=0), streamed)
    r = entries(base)[1]
    for relative, fmt, value in [(4,'<H',10),(6,'<H',1),(8,'<H',8),(10,'<H',1),
                                 (12,'<H',1),(14,'<I',123),(18,'<I',123),(22,'<I',123),(30,'<B',ord('x'))]:
        check(patch(base,r['l']+relative,fmt,value), ZIP)
    for flag in (1,0x40,0x2000,0x10,2):
        check(patch(base,r['c']+8,'<H',flag), ZIP)
    check(patch(base,r['c']+10,'<H',99), ZIP)
    check(patch(base,r['c']+6,'<H',45), ZIP)
    if streamed:
        end = r['d']+r['packed']
        for relative in (4,8,12): check(patch(base,end+relative,'<I',123),ZIP)
# CRC on native variants is mandatory even with a matching SHA in metadata.
for r in entries(data)[2:]:
    bad = patch(patch(data,r['c']+16,'<I',0),r['l']+14,'<I',0)
    check(bad,ZIP,zip_error=9)
directory = pack([*parts(), ('native/', b'', 8)])
r = entries(directory)[-1]
bad = patch(patch(directory,r['c']+16,'<I',123),r['l']+14,'<I',123)
check(bad,ZIP,zip_error=9)
# Bad compressed payload, short/long advertised size, compressed leftovers.
base = pack(parts()); r = entries(base)[1]
check(patch(base,r['d'],'<B',7),ZIP,zip_error=10)
for n in (0,1,len(BYTECODE)-1,len(BYTECODE)+1):
    bad = patch(patch(base,r['c']+24,'<I',n),r['l']+22,'<I',n)
    check(bad,ZIP,zip_error=8)
# Limit rejection of a huge declaration must not attempt a huge allocation.
bad = patch(patch(base,r['c']+24,'<I',0xfffffffe),r['l']+22,'<I',0xfffffffe)
check(bad,LIMIT,limit=1024*1024)
# Physical spans: prefix, suffix, overlaps, alias offsets, holes, descriptor absence.
check(b'prefix'+base,LAYOUT)
check(base+b'trailer',LAYOUT)
first = entries(base)[0]
check(patch(base,r['c']+42,'<I',first['l']),ZIP)
bad = patch(patch(base,first['c']+20,'<I',first['packed']+1),first['l']+18,'<I',first['packed']+1)
check(bad,LAYOUT)
bad = patch(patch(base,first['c']+20,'<I',first['packed']-1),first['l']+18,'<I',first['packed']-1)
check(bad,LAYOUT)
bad = patch(patch(base,r['c']+8,'<H',8),r['l']+6,'<H',8)
check(bad,LAYOUT)
streamed = pack(parts(),True); r = entries(streamed)[1]
check(patch(streamed,r['d']+r['packed'],'<I',0),LAYOUT)
# ZIP64 and multidisk metadata, local-only ZIP64 and truncation.
end = base.rfind(b'PK\x05\x06')
for off, fmt, value in [(end+10,'<H',65535),(end+4,'<H',1),(end+6,'<H',1),
                         (entries(base)[1]['c']+20,'<I',0xffffffff)]:
    check(patch(base,off,fmt,value),ZIP)
forced = io.BytesIO()
with zipfile.ZipFile(forced,'w') as z:
    for name,content,_ in parts():
        with z.open(name,'w',force_zip64=True) as out: out.write(content)
check(forced.getvalue(),ZIP,zip_error=4)
for n in range(len(base)):
    check(base[:n],ZIP)
# Deterministic damage must never crash, return partial output, or write files.
damage_base = (SAMPLES / 'container-damage-base.bin').read_bytes()
damage_cases = json.loads((SAMPLES / 'container-damage.json').read_text(encoding='utf-8'))
assert len(damage_cases) == 100
for mutations in damage_cases:
    assert len(mutations) == 3
    bad = bytearray(damage_base)
    for offset, mask in mutations:
        bad[offset] ^= mask
    check(bytes(bad),tuple(range(14)))
print(f'Module container checks passed: {COUNT}')
