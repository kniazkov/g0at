#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""File identity, scope and VM-entry checks using independently encoded images."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
OK, ARGUMENT, JSON, IMAGE, FILES, FUNCTIONS, PARAMETERS, IMPORTS = range(8)
COUNT = 0


def image(order, padding=0):
    instructions, files = [], [None] * len(order)
    for file_id in order:
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        instructions.extend([(10, 0, 0, 100 + file_id), (16, 0, 0, 0), (2, 0, 0, 0)])
        files[file_id] = (first, len(instructions) - first, entry, 0)
    locations = sorted([(first, count, i, 1, 1, 0)
                        for i, (first, count, entry, reserved) in enumerate(files)])
    payload = 'x\0'.encode('utf-32-le')
    od = 64 + len(instructions) * 8
    of = od + 16
    on = of + len(files) * 16
    start = on + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), 1, len(files), 0, len(locations),
                         64, od, of, on, on, start)
    result = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
              + struct.pack('<4I', 0, len(payload), 1, 1)
              + b''.join(struct.pack('<4I', *v) for v in files)
              + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['files'] = [dict(name=f'absent-{i}.goat', entry=v[2], external=False)
                         for i, v in enumerate(files)]
    return metadata, result


def check(metadata, code, status=OK):
    global COUNT
    raw = metadata if isinstance(metadata, bytes) else json.dumps(metadata, ensure_ascii=False).encode()
    data = struct.pack('<I', len(raw)) + raw + code
    proc = subprocess.run([EXE, 'run'], input=data, capture_output=True, timeout=20)
    COUNT += 1
    assert proc.returncode == status, (COUNT, status, proc.returncode, proc.stderr.decode(errors='replace'))
    if not status:
        main = metadata['main']
        assert proc.stdout.strip() == f'{main} {metadata["files"][main]["entry"]}'.encode()
    else:
        assert not proc.stdout


subprocess.run([EXE], check=True, timeout=20)
for order in [[0], [0, 1], [0, 1, 2], list(range(20))]:
    for padding in [0, 2]:
        metadata, code = image(order, padding)
        for main in range(len(order)):
            metadata['main'] = main
            check(metadata, code)

metadata, code = image([0, 1, 2], 1)
for main in [3, -1, 0xffffffff, None, '1', 1.0, True]:
    bad = copy.deepcopy(metadata)
    bad['main'] = main
    check(bad, code, JSON)
for field, value in [('entry', 0), ('entry', 999), ('entry', 2), ('entry', 5)]:
    bad = copy.deepcopy(metadata)
    bad['files'][0][field] = value
    check(bad, code, FILES)
for field, value in [('entry', None), ('entry', 1.0), ('entry', -1), ('external', 'true'),
                     ('name', None), ('id', 0)]:
    bad = copy.deepcopy(metadata)
    bad['files'][0][field] = value
    check(bad, code, JSON)
for name in ['same.goat', 'Ж😀.goat']:
    same = copy.deepcopy(metadata)
    for file in same['files']:
        file.update(name=name, external=True)
    check(same, code)
    same['files'][0]['external'] = False
    check(same, code)  # Internal/external labels may coincide, never merge identities.
    same['files'][1]['external'] = False
    check(same, code, JSON)
check(metadata, code[:-1], IMAGE)
check(metadata, bytes(len(code)), IMAGE)
check(b'{}', code, JSON)
print(f'Module file scopes: {COUNT} fixtures passed')
