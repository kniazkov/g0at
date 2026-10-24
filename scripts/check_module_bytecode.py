#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Independent portable module images and strict JSON/binary cross-checks."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VECTORS = ROOT / 'docs/module-format-v1/vectors'
EXE = str(Path(sys.argv[1]).resolve())
OK, ARGUMENT, JSON, IMAGE, FILES, FUNCTIONS, PARAMETERS, IMPORTS = range(8)
COUNT = 0
MAX = 0xffffffff
BASE = json.loads((VECTORS / 'empty.module.json').read_text())


def check(metadata, image, status=OK, entry=0):
    global COUNT
    raw = metadata if isinstance(metadata, bytes) else json.dumps(metadata, ensure_ascii=False).encode()
    data = struct.pack('<I', len(raw)) + raw + image
    result = subprocess.run([EXE, 'validate'], input=data, capture_output=True, timeout=20)
    COUNT += 1
    assert result.returncode == status, (COUNT, status, result.returncode, result.stderr[:2000])
    if status:
        assert not result.stdout, (COUNT, result.stdout)
    else:
        assert result.stdout.strip() == str(entry).encode(), (COUNT, result.stdout, entry)


def image(instructions, files, functions=(), locations=None, strings=(), vectors=()):
    """Encode the v1 wire layout independently; tuples use their on-disk order."""
    ni = len(instructions)
    payload, descriptors = bytearray(), []
    for text in strings:
        payload.extend(bytes((-len(payload)) % 8))
        raw = text.encode('utf-32-le') + bytes(4)
        descriptors.append((len(payload), len(raw), 1, len(text)))
        payload.extend(raw)
    for vector in vectors:
        payload.extend(bytes((-len(payload)) % 8))
        raw = struct.pack('<' + 'I' * len(vector), *vector)
        descriptors.append((len(payload), len(raw), 2, len(vector)))
        payload.extend(raw)
    if locations is None:
        locations = [(first, count, i, 1, 1, 0) for i, (first, count, entry, reserved) in enumerate(files)]
    od = 64 + ni * 8
    of = od + len(descriptors) * 16
    on = of + len(files) * 16
    ol = on + len(functions) * 32
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         ni, len(descriptors), len(files), len(functions), len(locations),
                         64, od, of, on, ol, start)
    return (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in files)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)


def instruction(op, arg1=0, arg0=0):
    return op, 0, arg0, arg1


def changed(metadata, key, value):
    result = copy.deepcopy(metadata)
    result[key] = value
    return result


subprocess.run([EXE], check=True, timeout=20)
minimal = (VECTORS / 'empty.bytecode.bin').read_bytes()
check(BASE, minimal)
check(changed(BASE, 'functions', []), minimal)
check(changed(BASE, 'mainHash', '0' * 64), minimal)  # Hashes are the container layer's job.
for stem in ['empty', 'two-versions']:
    check(json.loads((VECTORS / f'{stem}.module.json').read_text()),
          (VECTORS / f'{stem}.bytecode.bin').read_bytes())

# Main is a logical file index, not its instruction address; names do not open files.
files = [(0, 3, 1, 0), (3, 3, 4, 0)]
multi = image([instruction(0), instruction(73, 1), instruction(2),
               instruction(0), instruction(0), instruction(2)], files)
metadata = copy.deepcopy(BASE)
metadata['files'] = [{'name': 'not-on-disk.goat', 'entry': 1, 'external': True},
                     {'name': 'not-on-disk.goat', 'entry': 4, 'external': True}]
check(metadata, multi, entry=1)
check(changed(metadata, 'main', 1), multi, entry=4)
relocated = image([instruction(0), instruction(0), instruction(2),
                   instruction(0), instruction(73, 0), instruction(2)], files)
check(changed(metadata, 'main', 1), relocated, entry=4)
for value in [2, MAX, MAX + 1, -1, 0.0, None, '0']:
    check(changed(metadata, 'main', value), multi, JSON)
for value in [[], None, {}, 'files']:
    check(changed(metadata, 'files', value), multi, JSON)
missing = copy.deepcopy(metadata)
del missing['files']
check(missing, multi, JSON)
for key, value in [('id', 0), ('external', 'true'), ('entry', MAX + 1), ('entry', -1),
                   ('entry', 1.0), ('entry', '1'), ('entry', None)]:
    bad = copy.deepcopy(metadata)
    bad['files'][0][key] = value
    check(bad, multi, JSON)
for key in ['external', 'entry', 'name']:
    bad = copy.deepcopy(metadata)
    del bad['files'][0][key]
    check(bad, multi, JSON)
for value in [0, 2, 3, 6, MAX]:
    bad = copy.deepcopy(metadata)
    bad['files'][0]['entry'] = value
    check(bad, multi, FILES)
check(BASE, multi, FILES)
check(metadata, minimal, FILES)

# Two retained functions, with a legal gap and maximum uint32 identifier.
# The first parameter has supplementary Unicode and an embedded U+0000.
parameter = 'Ж\0😀'
ins = [instruction(1, 8), instruction(47, 1, 1), instruction(5),
       instruction(1, 10), instruction(47), instruction(5), instruction(2), instruction(0),
       instruction(7), instruction(50), instruction(7), instruction(50)]
functions = [(1, 0, 1, 8, 1, 1, MAX, 0), (MAX, 0, 4, 10, 0, 0, MAX, 0)]
locations = [(0, 8, 0, 1, 1, 0), (8, 2, 0, 2, 1, 1), (10, 2, 0, 3, 1, MAX)]
code = image(ins, [(0, 12, 0, 0)], functions, locations, [parameter], [(0,)])
funcmeta = copy.deepcopy(BASE)
funcmeta['functions'] = [{'functionId': 1, 'file': 0, 'instruction': 1,
                          'parameters': [{'name': parameter}]},
                         {'functionId': MAX, 'file': 0, 'instruction': 4, 'parameters': []}]
check(funcmeta, code)
check(changed(funcmeta, 'functions', list(reversed(funcmeta['functions']))), code)
for value in [parameter[:-1], 'Ж', 'Ж\0x', 'Ж\0😀x']:
    bad = copy.deepcopy(funcmeta)
    bad['functions'][0]['parameters'][0]['name'] = value
    check(bad, code, PARAMETERS)
for key, value in [('functionId', 3), ('instruction', 2), ('instruction', MAX), ('parameters', [])]:
    bad = copy.deepcopy(funcmeta)
    bad['functions'][0][key] = value
    check(bad, code, FUNCTIONS)
for key, value in [('functionId', 0), ('functionId', -1), ('functionId', MAX + 1),
                   ('functionId', '1'), ('functionId', 1.0), ('functionId', MAX),
                   ('file', 1), ('file', MAX), ('instruction', MAX + 1),
                   ('instruction', 1.0), ('parameters', None)]:
    bad = copy.deepcopy(funcmeta)
    bad['functions'][0][key] = value
    check(bad, code, JSON)
check(BASE, code, FUNCTIONS)
check(changed(BASE, 'functions', []), code, FUNCTIONS)
check(funcmeta, minimal, FUNCTIONS)
check(changed(funcmeta, 'functions', funcmeta['functions'][:1]), code, FUNCTIONS)
for value in [None, {}, 'functions']:
    check(changed(BASE, 'functions', value), minimal, JSON)
# Own exports resolve by functionId, never by array offset or count.
for fid in [1, MAX]:
    check(changed(funcmeta, 'externals', {'f': {'mutable': False, 'type': 'function', 'functionId': fid}}), code)
for fid in [0, 2, 3, MAX + 1]:
    check(changed(funcmeta, 'externals', {'f': {'mutable': False, 'type': 'function', 'functionId': fid}}), code, JSON)
check(changed(BASE, 'externals', {'f': {'mutable': False, 'functionId': 1}}), minimal, JSON)
# Foreign functionRef resolution is deliberately deferred to the checked dependency graph.
check(changed(BASE, 'externals', {'f': {'mutable': False, 'functionRef':
                                      {'module': 'other', 'version': '1', 'functionId': MAX}}}), minimal)
# Native metadata own-ID references are rejected by the strict JSON reader already.
native = json.loads((VECTORS / 'native-description.json').read_text())
variant = {k: native[k] for k in ['target', 'specializations']}
variant.update(library='native/test.so', hash='0' * 64, goatAbiVersion=1)
variant['specializations'][0]['signature']['arguments'] = ['integer']
variant['specializations'][0]['when'] = {'argumentTypes': ['integer']}
check(changed(funcmeta, 'native', [variant]), code)
bad = copy.deepcopy(funcmeta)
bad['native'] = [variant]
bad['native'][0]['specializations'][0]['functionId'] = 2
check(bad, code, JSON)

# Every IMPORT, even after END, must be fully qualified and belong
# to the source-derived dependency set. Duplicate uses are not duplicate deps.
def import_code(names):
    instructions = [instruction(2)]
    for i in range(len(names)):
        instructions.extend([instruction(74, i), instruction(5)])
    return image(instructions, [(0, len(instructions), 0, 0)], strings=names)


def dependency(name='math', version='1'):
    return {'name': name, 'version': version, 'hash': '0' * 64}


imports = changed(BASE, 'dependencies', [dependency()])
check(imports, import_code(['math@1']))
check(imports, import_code(['math@1', 'math@1']))
check(changed(BASE, 'dependencies', [dependency(version='2'), dependency()]),
      import_code(['math@1', 'math@2']))
for name in ['math', 'math@2', '@1', 'math@', 'math@@1', 'Math@1', 'math@1\0tail',
             'math@1/2', 'math@😀', 'math@1.', 'con@1']:
    check(imports, import_code([name]), IMPORTS)
check(BASE, import_code(['math@1']), IMPORTS)
check(imports, minimal)  # An optimizer may remove every IMPORT instruction.
check(changed(BASE, 'dependencies', [dependency(), dependency('unused')]),
      import_code(['math@1']))

# Corruptions are rejected without executing any user instruction.
for op, operand in [(255, 0), (3, MAX), (73, 1), (74, MAX), (13, 0), (0, 1)]:
    broken = image([instruction(2), instruction(op, operand)], [(0, 2, 0, 0)])
    check(BASE, broken, IMAGE)
for truncated in [b'', minimal[:8], minimal[:-1], minimal + b'\0']:
    check(BASE, truncated, IMAGE)
# Exact integer syntax: no exponents, fraction or string coercion.
raw = json.dumps(BASE).encode()
for value in [b'0e0', b'0.0', b'"0"', b'4294967296']:
    check(raw.replace(b'"main": 0', b'"main": ' + value), minimal, JSON)
print(f'Module bytecode validation: {COUNT} cases passed')
