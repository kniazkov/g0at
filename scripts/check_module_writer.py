#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Independent deterministic .gbin writer and publication checks."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import random
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
CODE = (VECTORS / 'empty.bytecode.bin').read_bytes()
TARGET = json.loads((VECTORS / 'target.json').read_text())
COUNT = 0
LAST_STATUS = 0
LAST_ERROR = b''
ALLOW_UNSUPPORTED_ACL = '--allow-unsupported-acl' in sys.argv[2:]
OK, ARGUMENT, JSON, PARTS, LIMIT, ZIP, VALIDATION, DESTINATION, IO, MEMORY = range(10)


def pack(metadata=BASE, code=CODE, payloads=None, reverse=False):
    parts = [('module.json', json.dumps(metadata, ensure_ascii=False, indent=2).encode()),
             ('bytecode.bin', code)] + list((payloads or {}).items())
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        for name, data in reversed(parts) if reverse else parts:
            z.writestr(name, data)
    return stream.getvalue()


def run(data, status=OK, destination=None, limits=None, fault=None):
    global COUNT, LAST_STATUS, LAST_ERROR
    COUNT += 1
    executable = EXE
    if fault:
        executable = str(Path(EXE).with_name('module_writer_fault_testing' + Path(EXE).suffix))
    args = [executable, 'file' if destination is not None else 'memory',
            str(destination) if destination is not None else 'unused']
    if limits:
        args += [str(n) for n in limits]
    p = subprocess.run(args, input=data, capture_output=True, timeout=30,
                       env=dict(os.environ, GOAT_WRITER_FAULT=fault) if fault else None)
    LAST_STATUS, LAST_ERROR = p.returncode, p.stderr
    expected = status if isinstance(status, tuple) else (status,)
    assert p.returncode in expected, (COUNT, p.returncode, expected, p.stderr)
    if p.returncode:
        assert not p.stdout, (COUNT, p.stdout)
    return p.stdout


def expected_parts(metadata, code, payloads):
    normalized = ref.normalize(metadata)
    renamed = {}
    for i, native in enumerate(normalized.get('native', []), 1):
        original = native['library']
        extension = '.dll' if native['target']['os'] == 'windows' else '.so'
        native['library'] = f'native/variant-{i}{extension}'
        renamed[native['library']] = payloads[original]
    normalized = ref.normalize(normalized)
    return dict({'module.json': ref.canonical(normalized), 'bytecode.bin': code}, **renamed)


def inspect(data, expected):
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = ['module.json', 'bytecode.bin'] + sorted(set(expected) - {'module.json', 'bytecode.bin'})
        assert z.namelist() == names, z.namelist()
        assert not z.comment
        for item in z.infolist():
            assert item.compress_type == zipfile.ZIP_DEFLATED
            assert item.date_time == (1980, 1, 1, 0, 0, 0)
            assert item.flag_bits == 0x800 and item.create_system == 3
            assert item.create_version == 20 and item.extract_version == 20
            assert item.internal_attr == 0 and item.external_attr == 0o100644 << 16
            assert not item.extra and not item.comment
            assert z.read(item) == expected[item.filename]
            # No data descriptor and exact local/central size and CRC fields.
            fields = struct.unpack_from('<I5H3I2H', data, item.header_offset)
            assert fields[:6] == (0x04034b50, 20, 0x800, 8, 0, 33)
            assert fields[6:9] == (item.CRC, item.compress_size, item.file_size)
            assert fields[-1] == 0
    assert data[-22:-18] == b'PK\x05\x06' and data[-2:] == b'\0\0'


subprocess.run([EXE], check=True)
golden = (VECTORS / 'empty.gbin').read_bytes()
encoded = run(pack())
assert encoded == golden, hashlib.sha256(encoded).hexdigest()
inspect(encoded, expected_parts(BASE, CODE, {}))
for reverse in (False, True):
    assert run(pack(reverse=reverse)) == golden
# Equivalent noncanonical metadata must normalize to the identical golden archive.
for field, value in [('dependencies', []), ('native', []), ('authors', []),
                     ('functions', []), ('externals', {}), ('description', ''),
                     ('license', ''), ('homepage', '')]:
    m = dict(copy.deepcopy(BASE), **{field: value})
    assert run(pack(m)) == golden
# Informational fields survive exactly, author order is not silently sorted.
m = dict(copy.deepcopy(BASE), description='Unicode: Привет 😀\n',
         authors=[dict(name='Zed'), dict(name='Анна', email='')])
expected = expected_parts(m, CODE, {})
result = run(pack(m)); inspect(result, expected)
assert run(pack(m, reverse=True)) == result

# A valid function-backed image supplies realistic specialization metadata.
def function_image():
    instructions = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
                    (7, 0, 0, 0), (50, 0, 0, 0)]
    of = 64 + len(instructions) * 8
    on, ol = of + 16, of + 48
    end = ol + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(instructions), 0, 1, 1, 2,
                        64, of, of, on, ol, end)
            + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + struct.pack('<4I', 0, len(instructions), 0, 0)
            + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
            + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
            + struct.pack('<6I', 4, 2, 0, 2, 1, 1))


code = function_image()
m = dict(copy.deepcopy(BASE), functions=[dict(functionId=1, file=0, instruction=1,
                                             parameters=[])], native=[])
m['mainHash'] = ref.main_hash(m, code)
rng = random.Random(183)
contents = [b'', b'x', b'xy', b'abc' * 10000, bytes(range(256)),
            bytes(rng.randrange(256) for _ in range(65537))]
# Twelve names exercise lexical variant-10 < variant-2 ordering, not numeric ZIP order.
payloads = {}
for i in range(12):
    name = f'native/input-{12-i}' + ('.dll' if i % 2 else '.so')
    payload = contents[i % len(contents)] + (bytes([i]) if i >= len(contents) else b'')
    payloads[name] = payload
    target = copy.deepcopy(TARGET)
    target['minimumOs'] = [5, 4, i, 0]
    target['cpuFeatures'] = ['sse2']
    if i % 2:
        target.update(os='windows', minimumOs=[10, 0, i, 0],
                      runtime=dict(family='ucrt', minimumVersion=[0, 0]))
    m['native'].append(dict(library=name, hash=hashlib.sha256(payload).hexdigest(),
        goatAbiVersion=1, target=target,
        specializations=[dict(functionId=1, specializationId=1, symbol='goat_fn_1_v1',
            signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
            when=dict(argumentTypes=[]))]))
expected = expected_parts(m, code, payloads)
full = run(pack(m, code, payloads)); inspect(full, expected)
# Frozen independent reference.py / zlib 1.3.2 output, not the host Python zlib.
# Every CI platform compares this same profile-1 archive containing empty,
# short, compressible and seeded incompressible native payloads.
assert hashlib.sha256(full).hexdigest() == '98617e59853dee57926692a6698f67c341eb69e9cc118e7b55ec746f3a6c5102'
assert run(pack(m, code, payloads, True)) == full
shuffled = copy.deepcopy(m); rng.shuffle(shuffled['native'])
assert run(pack(shuffled, code, dict(reversed(list(payloads.items()))))) == full
# Repack already normalized, renamed inputs without changing any archive byte/hash.
normalized = json.loads(expected['module.json'])
renamed_payloads = {k: v for k, v in expected.items() if k.startswith('native/')}
assert run(pack(normalized, code, renamed_payloads)) == full
assert normalized['mainHash'] == m['mainHash']
# Equal target/hash keys retain each compiler description with its original payload;
# at least ten aliases exercise filename tie-breaking after canonical renaming.
tied = copy.deepcopy(m)
tied_payloads = {}
for i, native in enumerate(tied['native']):
    native['library'] = f'native/tie-{i}.so'
    native['target'] = copy.deepcopy(TARGET)
    native['hash'] = hashlib.sha256(b'tied payload').hexdigest()
    native['cCompiler'] = dict(name='compiler', version=str(i))
    tied_payloads[native['library']] = b'tied payload'
tied_output = run(pack(tied, code, tied_payloads))
inspect(tied_output, expected_parts(tied, code, tied_payloads))
assert run(pack(tied, code, tied_payloads, True)) == tied_output

# Invalid supplied hashes/metadata/parts cannot be silently repaired or published.
bad_inputs = []
bad = copy.deepcopy(BASE); bad['mainHash'] = '0' * 64
bad_inputs.append(pack(bad))
bad = copy.deepcopy(BASE); bad['unknown'] = 1
bad_inputs.append(pack(bad))
bad = copy.deepcopy(BASE); bad['dependencies'] = None
bad_inputs.append(pack(bad))
bad_code = bytearray(CODE); bad_code[64] = 255
bad = copy.deepcopy(BASE); bad['mainHash'] = ref.main_hash(bad, bad_code)
bad_inputs.append(pack(bad, bad_code))
bad = copy.deepcopy(m); bad['native'][0]['hash'] = '0' * 64
bad_inputs.append(pack(bad, code, payloads))
bad_inputs.append(pack(m, code, dict(list(payloads.items())[1:])))
for wrong_name in ('native/wrong.dll', 'native/con.so'):
    bad = copy.deepcopy(m)
    old_name = bad['native'][0]['library']
    bad['native'][0]['library'] = wrong_name
    bad_payloads = dict(payloads); bad_payloads[wrong_name] = bad_payloads.pop(old_name)
    bad_inputs.append(pack(bad, code, bad_payloads))
bad_inputs.append(pack(BASE, CODE, {'native/extra.so': b'extra'}))
bad = copy.deepcopy(m); bad['native'][0]['specializations'][0]['functionId'] = 999
bad_inputs.append(pack(bad, code, payloads))
for data in bad_inputs:
    run(data, (ARGUMENT, JSON, PARTS, VALIDATION))
for maximum in (0, 1, len(golden) - 1):
    run(pack(), (LIMIT, ZIP, VALIDATION), limits=(maximum, 1000000))
assert run(pack(), limits=(len(golden), 1000000)) == golden
run(pack(), (LIMIT, VALIDATION), limits=(1000000, 1))

with tempfile.TemporaryDirectory(prefix='goat-writer-') as directory:
    directory = Path(directory)
    destination = directory / 'результат без расширения'
    run(pack(), destination=destination)
    assert destination.read_bytes() == golden
    if os.name != 'nt':
        destination.chmod(0o640)
    run(pack(m, code, payloads), status=(OK, IO) if ALLOW_UNSUPPORTED_ACL else OK,
        destination=destination)
    replacement_supported = LAST_STATUS == OK
    if not replacement_supported:
        assert b'FS 5' in LAST_ERROR, LAST_ERROR
        print('Explicit local-only skip: filesystem does not support required ACL transfer')
    current = full if replacement_supported else golden
    assert destination.read_bytes() == current
    if os.name != 'nt':
        assert stat.S_IMODE(destination.stat().st_mode) == 0o640
    if replacement_supported:
        alias = directory / 'old-hard-link'
        os.link(destination, alias)
        run(pack(), destination=destination)
        assert destination.read_bytes() == golden and alias.read_bytes() == full
        alias.unlink()
        run(pack(m, code, payloads), destination=destination)
    # Explicit same input/output: read all input, then publish from retained parts.
    if replacement_supported:
        source = destination.read_bytes()
        run(source, destination=destination)
        assert destination.read_bytes() == full
    # Every pre-publication failure preserves the old bytes and leaves no temporary file.
    initial = set(directory.iterdir())
    for data in bad_inputs:
        run(data, (ARGUMENT, JSON, PARTS, VALIDATION), destination)
        assert destination.read_bytes() == current and set(directory.iterdir()) == initial
    run(pack(), (LIMIT, ZIP, VALIDATION), destination, limits=(1, 1000000))
    assert destination.read_bytes() == current and set(directory.iterdir()) == initial
    run(pack(), (IO, DESTINATION), directory / 'missing-parent' / 'output')
    run(pack(), (IO, DESTINATION), directory)
    assert set(directory.iterdir()) == initial
    for failure in ('write', 'close', 'validation', 'prepare', 'rename'):
        run(pack(), VALIDATION if failure == 'validation' else IO, destination, fault=failure)
        assert destination.read_bytes() == current and set(directory.iterdir()) == initial
    target = directory / 'target'; target.write_bytes(b'keep target')
    link = directory / 'link'
    try:
        link.symlink_to(target)
    except OSError as error:
        # Only the explicit Windows privilege limitation permits a skipped fixture.
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1314:
            raise
    else:
        run(pack(), DESTINATION, link)
        assert link.is_symlink() and target.read_bytes() == b'keep target'
        link.unlink()
    if os.name != 'nt':
        fifo = directory / 'fifo'; os.mkfifo(fifo)
        run(pack(), DESTINATION, fifo)
        assert stat.S_ISFIFO(fifo.stat().st_mode)
        fifo.unlink()
print(f'{COUNT} deterministic module writer checks passed')
