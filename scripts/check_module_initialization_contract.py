#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Blocking initializer and original exception contract (decisions 141--142).

READY/release is an explicit process handshake, not a timing guess about startup.
One hold exceeds the formerly discussed five-second initialization deadline.
All other cases release immediately. Only the test runner has a watchdog.
"""
import copy
import json
from pathlib import Path
import queue
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
COUNT = 0


def image(padding, external, success):
    descriptors, payload = [], bytearray()
    for name in ['payload', 'counter']:
        payload.extend(bytes((-len(payload)) % 8))
        blob = (name + '\0').encode('utf-32-le')
        descriptors.append((len(payload), len(blob), 1, len(name)))
        payload.extend(blob)
    instructions, files, locations = [], [], []
    for file in range(2):
        first = len(instructions)
        instructions.extend([(0, 0, 0, 0)] * padding)
        entry = len(instructions)
        if file == 0:
            instructions.extend([(15, 0, 0, 1), (23, 0, 0, 0),
                                 (18, 0, 0, 1), (5, 0, 0, 0)])
            if not success:
                instructions.extend([(15, 0, 0, 0), (55, 0, 0, 0)])
        instructions.append((2, 0, 0, 0))
        files.append((first, len(instructions) - first, entry, 0))
        locations.append((first, len(instructions) - first, file, 20 + file, 7, 0))
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    ol = of + len(files) * 16
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), 2, 0, len(locations),
                         64, od, of, ol, ol, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in files)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    metadata = copy.deepcopy(BASE)
    metadata['files'] = [dict(name='same😀.goat' if external else f'file-{i}.goat',
                              entry=f[2], external=external) for i, f in enumerate(files)]
    return json.dumps(metadata, ensure_ascii=False).encode(), code


def check(kind, padding, external, hold):
    metadata, code = image(padding, external, kind == 'success')
    process = subprocess.Popen([EXE, kind], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ready = queue.Queue()
    reader = threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True)
    reader.start()
    try:
        process.stdin.write(struct.pack('<II', len(metadata), len(code)) + metadata + code)
        process.stdin.flush()
        assert ready.get(timeout=60).strip() == b'READY', (kind, padding, external, 'no READY')
        reader.join(timeout=5)
        assert not reader.is_alive()
        if hold:
            started = time.monotonic()
            time.sleep(5.3)
            assert time.monotonic() - started >= 5.0
        assert process.poll() is None, (kind, padding, external, 'terminated before release')
        output, error = process.communicate(b'R', timeout=60)
        assert process.returncode == 0, (kind, padding, external, process.returncode, output, error)
        assert output.strip() == b'OK', (kind, output, error)
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate()
        reader.join(timeout=5)


for kind in ['object', 'null', 'integer', 'success']:
    for padding in [0, 5]:
        for external in [False, True]:
            check(kind, padding, external, kind == 'object' and not padding and not external)
            COUNT += 1
print(f'Module initialization contract: {COUNT} fixtures passed')
