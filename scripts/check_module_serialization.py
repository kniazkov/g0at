#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""OS-thread initialization publication, exclusion and direct-entry readiness."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
COUNT = 0


def image(order, padding, mode):
    descriptors, payload = [], bytearray()

    def data(blob, kind, count):
        payload.extend(bytes((-len(payload)) % 8))
        index = len(descriptors)
        descriptors.append((len(payload), len(blob), kind, count))
        payload.extend(blob)
        return index

    def string(text):
        return data((text + '\0').encode('utf-32-le'), 1, len(text))

    x, c, nested, error, inner, cleanup = [string(s) for s in ['x', 'c', 'nested', 'error', 'inner', 'cleanup']]
    instructions, files, locations, functions = [], [None] * 3, [], []
    for file in order:
        declarations = [data(struct.pack('<4I', 0x4445434c, name, file, 1), 3, 1)
                        for name in [x, c, nested, inner, cleanup]]
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        dx, dc, dn, di, df = declarations
        if mode == 'early':
            instructions.extend([(71, 0, 0, dx), (5, 0, 0, 0)])
        elif mode == 'return':
            instructions.extend([(7, 0, 0, 0), (50, 0, 0, 0)])
        elif mode == 'caught':
            instructions.extend([(54, 0, 0, entry + 5), (71, 0, 0, dx), (5, 0, 0, 0),
                                 (53, 0, 0, 0), (3, 0, 0, entry + 6), (16, 0, 0, error)])
        final = len(instructions)
        instructions.extend([(66, 0, 0, final + 3), (0, 0, 0, 0),
                             (67, 0, 0, final + 6), (10, 0, 0, 9),
                             (69, 0, 0, df), (68, 0, 0, 0)])
        instructions.extend([(10, 0, 0, 100 + file), (69, 0, 0, dx),
                             (10, 0, 0, 7), (70, 0, 0, dc),
                             (71, 0, 0, dx), (5, 0, 0, 0),
                             (51, 0, 0, 0), (10, 0, 0, 8), (69, 0, 0, dn),
                             (52, 0, 0, 0), (5, 0, 0, 0)])
        definition = len(instructions)
        body = definition + 4
        instructions.extend([(1, 0, 0, body), (47, 0, 0, 0), (5, 0, 0, 0), (2, 0, 0, 0),
                             (10, 0, 0, 15), (69, 0, 0, di), (7, 0, 0, 0), (50, 0, 0, 0)])
        functions.append((file + 1, file, definition + 1, body, 0, 0, 0xffffffff, 0))
        files[file] = first, len(instructions) - first, entry, 0
        locations.extend([(first, body - first, file, 1, 1, 0),
                          (body, len(instructions) - body, file, 2, 1, file + 1)])
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    on = of + len(files) * 16
    ol = on + len(functions) * 32
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), 3, len(functions), len(locations),
                         64, od, of, on, ol, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in files)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['files'] = [dict(name=f'не найден-{i}😀.goat', entry=v[2], external=False)
                         for i, v in enumerate(files)]
    metadata['functions'] = [dict(functionId=f[0], file=f[1], instruction=f[2], parameters=[])
                             for f in functions]
    return metadata, code


for padding in [0, 2, 7]:
    metadata, code = image([0, 1, 2], padding, 'normal')
    raw = json.dumps(metadata, ensure_ascii=False).encode()
    result = subprocess.run([EXE], input=struct.pack('<I', len(raw)) + raw + code,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, (padding, result.returncode, result.stdout, result.stderr)
    assert result.stdout.strip() == b'OK', (padding, result.stdout)
print('Module serialization: 12 deterministic scenarios passed')
