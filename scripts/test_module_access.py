#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""File/module access policy, including independent multi-file wire fixtures."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())


def image(order, padding=0, modifier=None, constant=False):
    instructions, files = [], [None] * len(order)
    for file_id in order:
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        declaration = (16, 0, 0, 0) if modifier is None else (70 if constant else 69, 0, 0, file_id + 1)
        instructions.extend([(10, 0, 0, 100 + file_id), declaration, (2, 0, 0, 0)])
        files[file_id] = (first, len(instructions) - first, entry, 0)
    locations = sorted([(first, count, i, 1, 1, 0)
                        for i, (first, count, entry, reserved) in enumerate(files)])
    payload = 'x\0'.encode('utf-32-le')
    descriptors = [(0, len(payload), 1, 1)]
    if modifier is not None:
        for file_id in range(len(files)):
            descriptors.append((len(payload), 16, 3, 1))
            payload += struct.pack('<4I', 0x4445434c, 0, file_id, modifier)
    od = 64 + len(instructions) * 8
    of = od + 16 * len(descriptors)
    on = of + len(files) * 16
    start = on + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), len(files), 0, len(locations),
                         64, od, of, on, on, start)
    result = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
              + b''.join(struct.pack('<4I', *v) for v in descriptors)
              + b''.join(struct.pack('<4I', *v) for v in files)
              + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['files'] = [dict(name=f'absent-{i}.goat', entry=v[2], external=False)
                         for i, v in enumerate(files)]
    return metadata, result


def check(metadata, code):
    raw = json.dumps(metadata, ensure_ascii=False).encode()
    subprocess.run([EXE, 'wire'], input=struct.pack('<I', len(raw)) + raw + code,
                   check=True, timeout=20)


subprocess.run([EXE], check=True, timeout=30)
for order in [[0], [0, 1], [0, 1, 2], list(range(20))]:
    for padding in [0, 2]:
        metadata, code = image(order, padding)
        metadata['main'] = order[-1]
        check(metadata, code)
for modifier in [0, 1, 2]:
    for constant in [False, True]:
        for padding in [0, 2]:
            metadata, code = image([0, 1, 2], padding, modifier, constant)
            metadata['main'] = 2
            check(metadata, code)
print('Module access: model/VM cases and 20 independent file images passed')
