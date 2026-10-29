#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Temporary snapshot fixtures for check_module_identity.py (#354; cleanup #394)."""
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
TARGET = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())['target']


def image(value, label, files=1, throwing=False, native=False, dormant_import=None):
    descriptors, payload = [], bytearray()

    def data(blob, kind, count):
        payload.extend(bytes((-len(payload)) % 8))
        index = len(descriptors)
        descriptors.append((len(payload), len(blob), kind, count))
        payload.extend(blob)
        return index

    def string(text):
        return data((text + '\0').encode('utf-32-le'), 1, len(text))

    x, label_key, label_value = string('x'), string('label'), string(label)
    import_string = string(dormant_import) if dormant_import else None
    instructions, file_records, functions, locations = [], [], [], []
    for file in range(files):
        start = len(instructions)
        declaration = data(struct.pack('<4I', 0x4445434c, x, file, 0), 3, 1)
        instructions.extend([(10, 0, 0, value + file), (69, 0, 0, declaration),
                             (13, 0, 0, label_value), (16, 0, 0, label_key)])
        if dormant_import and file == 0:
            instructions.extend([(3, 0, 0, len(instructions) + 3),
                                 (74, 0, 0, import_string), (5, 0, 0, 0)])
        if native and file == 0:
            instruction = len(instructions) + 1
            instructions.extend([(1, 0, 0, len(instructions) + 4),
                                 (47, 0, 0, 0), (5, 0, 0, 0), (2, 0, 0, 0),
                                 (7, 0, 0, 0), (50, 0, 0, 0)])
            functions.append((1, file, instruction, instruction + 3, 0, 0, 0xffffffff, 0))
            locations.extend([(start, instruction + 3 - start, file, 1, 1, 0),
                              (instruction + 3, 2, file, 2, 1, 1)])
        else:
            if throwing:
                instructions.extend([(10, 0, 0, 73), (55, 0, 0, 0)])
            instructions.append((2, 0, 0, 0))
            locations.append((start, len(instructions) - start, file, 1, 1, 0))
        file_records.append((start, len(instructions) - start, start, 0))
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    on = of + files * 16
    ol = on + len(functions) * 32
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), files, len(functions), len(locations),
                         64, od, of, on, ol, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in file_records)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    return code, file_records, functions


def archive(name, version='1', dependencies=(), value=100, label='старый 😀',
            files=1, throwing=False, native=False, dormant_import=None):
    code, file_records, functions = image(value, label, files, throwing, native, dormant_import)
    metadata = dict(copy.deepcopy(BASE), name=name, version=version, main=files - 1,
                    files=[dict(name=f'absent-{i}.goat', entry=v[2], external=False)
                           for i, v in enumerate(file_records)])
    if dependencies:
        metadata['dependencies'] = list(dependencies)
    if functions:
        metadata['functions'] = [dict(functionId=f[0], file=f[1], instruction=f[2], parameters=[])
                                 for f in functions]
    libraries = {}
    if native:
        libraries['native/opaque.so'] = ('never OS-load: ' + label).encode()
        metadata['native'] = [dict(library='native/opaque.so',
                                  hash=hashlib.sha256(libraries['native/opaque.so']).hexdigest(),
                                  goatAbiVersion=1, target=TARGET,
                                  specializations=[dict(functionId=1, specializationId=1,
                                      symbol='goat_fn_1_v1',
                                      signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                                      when=dict(argumentTypes=[]))])]
    metadata['mainHash'] = ref.main_hash(metadata, code)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(metadata, ensure_ascii=False).encode())
        z.writestr('bytecode.bin', code)
        for path, blob in libraries.items():
            z.writestr(path, blob)
    record = dict(name=name, version=version, hash=metadata['mainHash'])
    return stream.getvalue(), record


def put(path, blob):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)
    return path
