#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Exhaust every snapshot allocation site in isolated, bounded subprocesses."""
import copy
import hashlib
import io
import json
from pathlib import Path
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
TARGET = json.loads((VECTORS / 'native-description.json').read_text())['target']
COUNT = 0


def archive(name, dependencies=(), code=CODE, native=False):
    metadata = dict(copy.deepcopy(BASE), name=name, version='1')
    libraries = {}
    if native:
        instructions = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0),
                        (0, 0, 0, 0), (7, 0, 0, 0), (50, 0, 0, 0)]
        # The dependency/function is retained, not executed in this test.
        files = 64 + len(instructions) * 8
        functions, locations = files + 16, files + 48
        end = locations + 48
        code = (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, 6, 0, 1, 1, 2,
                            64, files, files, functions, locations, end)
                + b''.join(struct.pack('<BBHI', *item) for item in instructions)
                + struct.pack('<4I', 0, 6, 0, 0)
                + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
                + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
                + struct.pack('<6I', 4, 2, 0, 2, 1, 1))
        metadata['functions'] = [dict(functionId=1, file=0, instruction=1, parameters=[])]
        payload = b'Opaque native image: must never be OS-loaded during preflight.'
        libraries['native/test.so'] = payload
        metadata['native'] = [dict(library='native/test.so',
                                  hash=hashlib.sha256(payload).hexdigest(),
                                  goatAbiVersion=1, target=TARGET,
                                  specializations=[dict(functionId=1, specializationId=1,
                                      symbol='goat_fn_1_v1',
                                      signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                                      when=dict(argumentTypes=[]))])]
    if dependencies:
        metadata['dependencies'] = list(dependencies)
    metadata['mainHash'] = ref.main_hash(metadata, code)
    parts = {'module.json': json.dumps(metadata).encode(), 'bytecode.bin': code, **libraries}
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as stream:
        for path, data in parts.items():
            stream.writestr(path, data)
    return (output.getvalue(), dict(name=name, version='1', hash=metadata['mainHash']),
            sum(map(len, parts.values())))


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def invoke(directory, arguments):
    global COUNT
    COUNT += 1
    result = subprocess.run([EXE, *map(str, arguments)], cwd=directory,
                            capture_output=True, timeout=30)
    assert b'ERROR: AddressSanitizer' not in result.stderr and b'runtime error:' not in result.stderr, (
        COUNT, result.returncode, result.stderr)
    return result


def normal(directory, root, status=0, limits=()):
    result = invoke(directory, [root, 0, *limits])
    assert result.returncode == 0, (COUNT, result.returncode, result.stdout, result.stderr)
    lines = result.stdout.splitlines()
    info = json.loads(lines[-1])
    assert info['status'] == status and not info['triggered'], (COUNT, info, result.stderr)
    assert (b'EXECUTED' in lines) == (status == 0), (COUNT, result.stdout)
    return info


with tempfile.TemporaryDirectory(prefix='goat-memory-') as temporary:
    directory = Path(temporary)
    root = directory / 'root.gbin'
    dep, dep_record, dep_expanded = archive('dep', native=True)
    blob, _, root_expanded = archive('app', [dep_record])
    put(root, blob)
    put(directory / 'modules/dep/1.gbin', dep)
    # A healthy later copy must never rescue memory failure on the first copy.
    put(directory / 'fallback/dep/1.gbin', dep)
    baseline = normal(directory, root)
    assert baseline['checks'] == 2, baseline
    returned = terminated = 0
    for mode in ([], ['once']):
        for index in range(1, baseline['allocations'] + 1):
            result = invoke(directory, [root, index, *mode])
            assert b'EXECUTED' not in result.stdout, (index, result.stdout, result.stderr)
            if result.returncode:
                assert result.returncode == 1 and b'Out of memory' in result.stderr, (
                    index, result.returncode, result.stdout, result.stderr)
                terminated += 1
            else:
                info = json.loads(result.stdout)
                assert info['status'] == 3 and info['triggered'], (index, info, result.stderr)
                returned += 1
    assert returned and terminated, (returned, terminated)
    last = invoke(directory, [root, baseline['allocations'] + 1])
    assert last.returncode == 0 and b'EXECUTED' in last.stdout, last.stderr

    bits = invoke(directory, ['boundaries'])
    assert bits.returncode == 0 and bits.stdout.strip() in (b'32', b'64'), bits.stderr
    maximum = (1 << int(bits.stdout)) - 1
    for mode in ('add-overflow', 'multiply-overflow'):
        result = invoke(directory, [mode])
        assert result.returncode == 1 and b'Allocation size overflow' in result.stderr, (
            mode, result.returncode, result.stdout, result.stderr)
    normal(directory, root, limits=(max(len(blob), len(dep)), max(root_expanded, dep_expanded)))
    normal(directory, root, 2, limits=(len(blob) - 1, maximum))
    normal(directory, root, 2, limits=(maximum, root_expanded - 1))
    assert dep_expanded > root_expanded
    limited = normal(directory, root, 2, limits=(maximum, dep_expanded - 1))
    assert limited['checks'] == 2, limited

    # On every width, huge table counts must be rejected before indexing a tiny image.
    for field in (20, 24, 28, 32, 36):
        malformed = bytearray(CODE)
        struct.pack_into('<I', malformed, field, 0xffffffff)
        bad, _, _ = archive('app', code=bytes(malformed))
        put(root, bad)
        normal(directory, root, 2)

    # Synthetic declared sizes exercise overflow/limit preflight, not huge allocations.
    original, _, _ = archive('app')
    bad = bytearray(original)
    start = 0
    while True:
        offset = bad.find(b'PK\x01\x02', start)
        if offset < 0:
            break
        struct.pack_into('<I', bad, offset + 24, 0xfffffffe)
        local = struct.unpack_from('<I', bad, offset + 42)[0]
        struct.pack_into('<I', bad, local + 22, 0xfffffffe)
        start = offset + 46
    put(root, bad)
    normal(directory, root, 2, limits=(maximum, 0xffffffff))

print(f'module memory: {COUNT} checks passed; {returned} controlled returns, '
      f'{terminated} fatal allocation failures, size_t={int(bits.stdout)}')
