#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Rules 41--45 over independent three-file images and real VM copy operations."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())


def image(main, padding):
    names = ['child', 'flatfn', 'result', 'error', 'own', 'seen', 'payload', 'clone', 'copied']
    payload, descriptors = bytearray(), []
    for name in names:
        payload.extend(bytes((-len(payload)) % 8))
        blob = (name + '\0').encode('utf-32-le')
        descriptors.append((len(payload), len(blob), 1, len(name)))
        payload.extend(blob)
    instructions, files, locations = [], [], []
    observer = (main + 2) % 3
    for file in range(3):
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        if file == observer:
            instructions.extend([(15, 0, 0, 0), (6, 0, 0, 0), (19, 0, 0, 7),
                                 (49, 0, 0, 0), (18, 0, 0, 8), (5, 0, 0, 0)])
            handler = len(instructions) + 8
            end = handler + 2
            instructions.extend([(54, 0, 0, handler), (15, 0, 0, 0),
                                 (15, 0, 0, 1), (49, 0, 0, 0),
                                 (18, 0, 0, 2), (5, 0, 0, 0),
                                 (53, 0, 0, 0), (3, 0, 0, end),
                                 (18, 0, 0, 3), (5, 0, 0, 0),
                                 (15, 0, 0, 0), (19, 0, 0, 4),
                                 (18, 0, 0, 5), (5, 0, 0, 0)])
        instructions.append((2, 0, 0, 0))
        files.append((first, len(instructions) - first, entry, 0))
        locations.append((first, len(instructions) - first, file, 10 + file, 2, 0))
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    on = of + len(files) * 16
    start = on + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), 3, 0, len(locations),
                         64, od, of, on, on, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in files)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['main'] = main
    metadata['files'] = [dict(name=f'prototype-{i}😀.goat', entry=f[2], external=False)
                         for i, f in enumerate(files)]
    return metadata, code


def main():
    exe = str(Path(sys.argv[1]).resolve())
    count = 0
    for primary in range(3):
        for padding in [0, 5]:
            for shape in range(4):
                for failed in [False, True]:
                    for payload_kind in ['object', 'null']:
                        metadata, code = image(primary, padding)
                        raw = json.dumps(metadata, ensure_ascii=False).encode()
                        result = subprocess.run([exe, str(shape), str(int(failed)), payload_kind],
                                                input=struct.pack('<I', len(raw)) + raw + code,
                                                capture_output=True, timeout=30)
                        count += 1
                        assert result.returncode == 0, (count, primary, padding, shape,
                                                        failed, payload_kind, result.returncode,
                                                        result.stdout, result.stderr)
                        assert result.stdout.strip() == b'OK', (count, result.stdout)
    print(f'Module failed prototypes: {count} fixtures passed')


if __name__ == '__main__':
    main()
