#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Rules 26--35 over independent bytecode images and the real VM/state gate."""
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


def image(rule, padding, external):
    names = ['payload', 'replacement', 'cleanup', 'caught', 'function', 'child']
    payload, descriptors = bytearray(), []
    for name in names:
        payload.extend(bytes((-len(payload)) % 8))
        blob = (name + '\0').encode('utf-32-le')
        descriptors.append((len(payload), len(blob), 1, len(name)))
        payload.extend(blob)
    instructions, files, locations, functions = [], [], [], []
    for file in range(3):
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        mode = 'success'
        if (rule in [26, 27, 28] and file == 0) or (rule == 33 and file == 1):
            mode = 'replace' if rule == 33 else 'catch'
        elif (rule == 27 and file == 1) or (rule in [28, 32, 33] and file == 2):
            mode = 'throw'
        elif rule in [30, 31] and file == 0:
            mode = 'throw'
        elif rule in [34, 35] and file == 0:
            mode = 'finally-replace' if rule == 34 else 'finally'
        elif rule == 29 and file == 0:
            mode = 'function'
        if mode in ['catch', 'replace']:
            throwing = [(15, 0, 0, 0), (55, 0, 0, 0)] if rule == 26 else [(15, 0, 0, 5), (19, 0, 0, 0)]
            instructions.extend([(54, 0, 0, entry + 5)] + throwing + [
                                 (53, 0, 0, 0), (3, 0, 0, entry + (12 if mode == 'replace' else 10)),
                                 (5, 0, 0, 0), (15, 0, 0, 3), (23, 0, 0, 0),
                                 (18, 0, 0, 3), (5, 0, 0, 0)])
            if mode == 'replace':
                instructions.extend([(15, 0, 0, 1), (55, 0, 0, 0)])
        elif mode == 'throw':
            instructions.extend([(15, 0, 0, 0), (55, 0, 0, 0)])
        elif mode.startswith('finally'):
            instructions.extend([(66, 0, 0, entry + 3), (15, 0, 0, 0), (55, 0, 0, 0),
                                 (15, 0, 0, 2), (23, 0, 0, 0), (18, 0, 0, 2), (5, 0, 0, 0)])
            if mode == 'finally-replace':
                instructions.extend([(15, 0, 0, 1), (55, 0, 0, 0)])
            else:
                instructions.append((68, 0, 0, 0))
        elif mode == 'function':
            body = entry + 4
            instructions.extend([(1, 0, 0, body), (47, 0, 0, 0), (16, 0, 0, 4),
                                 (2, 0, 0, 0), (15, 0, 0, 0), (55, 0, 0, 0)])
            functions.append((1, file, entry + 1, body, 0, 0, 0xffffffff, 0))
        instructions.append((2, 0, 0, 0))
        files.append((first, len(instructions) - first, entry, 0))
        if mode == 'function':
            locations.extend([(first, body - first, file, 10 + file, 3, 0),
                              (body, len(instructions) - body, file, 20, 5, 1)])
        else:
            locations.append((first, len(instructions) - first, file, 10 + file, 3, 0))
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
    metadata['files'] = [dict(name='same😀.goat' if external else f'file-{i}.goat',
                              entry=f[2], external=external) for i, f in enumerate(files)]
    metadata['functions'] = [dict(functionId=f[0], file=f[1], instruction=f[2], parameters=[])
                             for f in functions]
    return metadata, code


for rule in range(26, 36):
    for padding in [0, 5]:
        for external in [False, True]:
            for kind in ['object', 'null', 'integer']:
                metadata, code = image(rule, padding, external)
                raw = json.dumps(metadata, ensure_ascii=False).encode()
                result = subprocess.run([EXE, str(rule), kind],
                                        input=struct.pack('<I', len(raw)) + raw + code,
                                        capture_output=True, timeout=30)
                COUNT += 1
                assert result.returncode == 0, (COUNT, rule, kind, result.returncode,
                                                result.stdout, result.stderr)
                assert result.stdout.strip() == b'OK', (COUNT, result.stdout)
print(f'Module exception propagation: {COUNT} fixtures passed')
