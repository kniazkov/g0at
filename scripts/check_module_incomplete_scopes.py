#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Rules 36--40 in independently encoded images and the real VM/file gate."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())


def image(rule, caught, main, padding, flat, constant):
    names = ['x', 'secret', 'target', 'seen', 'child', 'own', 'caught', 'after',
             'clone', 'flat', 'error']
    payload, descriptors = bytearray(), []

    def data(blob, kind, count):
        payload.extend(bytes((-len(payload)) % 8))
        index = len(descriptors)
        descriptors.append((len(payload), len(blob), kind, count))
        payload.extend(blob)
        return index

    for name in names:
        data((name + '\0').encode('utf-32-le'), 1, len(name))
    dx = data(struct.pack('<4I', 0x4445434c, 0, main, 1), 3, 1)
    ds = data(struct.pack('<4I', 0x4445434c, 1, main, 4), 3, 1)
    instructions, files, locations = [], [None, None], []
    for file in [0, 1]:
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        if file == main:
            instructions.extend([(10, 0, 0, 10), (70 if constant else 69, 0, 0, dx),
                                 (10, 0, 0, 55), (69, 0, 0, ds)])
        else:
            instructions.extend([(15, 0, 0, 2), (64, 0, 0, 0), (16, 0, 0, 3)])
            if rule == 40:
                instructions.extend([(15, 0, 0, 2), (59, 0, 1, 1), (10, 0, 0, 5),
                                     (16, 0, 0, 5), (52, 0, 0, 0), (16, 0, 0, 4)])
            if rule in [36, 38, 40]:
                body = [(15, 0, 0, 4 if rule == 40 else 2), (19, 0, 0, 0), (5, 0, 0, 0)]
            elif rule == 37:
                body = [(10, 0, 0, 20), (15, 0, 0, 2), (20, 0, 0, 0), (5, 0, 0, 0)]
            else:
                body = [(15, 0, 0, 2), (6, 0, 0, 0), (19, 0, 0, 9 if flat else 8),
                        (49, 0, 0, 0), (5, 0, 0, 0)]
            if caught:
                start = len(instructions)
                handler = start + 1 + len(body) + 2
                end = handler + 6
                instructions.extend([(54, 0, 0, handler)] + body
                                    + [(53, 0, 0, 0), (3, 0, 0, end),
                                       (18, 0, 0, 10), (5, 0, 0, 0), (15, 0, 0, 6), (23, 0, 0, 0),
                                       (18, 0, 0, 6), (5, 0, 0, 0)])
            else:
                instructions.extend(body)
            instructions.extend([(15, 0, 0, 7), (23, 0, 0, 0), (18, 0, 0, 7), (5, 0, 0, 0)])
        instructions.append((2, 0, 0, 0))
        files[file] = (first, len(instructions) - first, entry, 0)
        locations.append((first, len(instructions) - first, file, 10 + file, 2, 0))
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    on = of + len(files) * 16
    ol = on
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), 2, 0, len(locations),
                         64, od, of, on, ol, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in files)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['main'] = main
    metadata['files'] = [dict(name=f'scope-{i}😀.goat', entry=f[2], external=False)
                         for i, f in enumerate(files)]
    return metadata, code


def main():
    exe = str(Path(sys.argv[1]).resolve())
    count = 0
    for rule in range(36, 41):
        for caught in [False, True]:
            for primary in [0, 1]:
                for padding in [0, 4]:
                    for variant in [False, True]:
                        metadata, code = image(rule, caught, primary, padding, variant, variant)
                        raw = json.dumps(metadata, ensure_ascii=False).encode()
                        result = subprocess.run([exe, str(rule), str(int(caught)), str(int(variant))],
                                                input=struct.pack('<I', len(raw)) + raw + code,
                                                capture_output=True, timeout=30)
                        count += 1
                        assert result.returncode == 0, (count, rule, caught, primary, padding,
                                                        variant, result.returncode, result.stdout,
                                                        result.stderr)
                        assert result.stdout.strip() == b'OK', (count, result.stdout)
    print(f'Module incomplete scopes: {count} fixtures passed')


if __name__ == '__main__':
    main()
