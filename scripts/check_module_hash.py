"""Production main SHA-256 versus frozen vectors and Python hashlib/reference."""
import copy
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
COUNT = 0


def run(metadata, bytecode=b'', mode='hash', status=0):
    global COUNT
    raw = metadata if isinstance(metadata, bytes) else json.dumps(metadata, ensure_ascii=False).encode()
    p = subprocess.run([EXE, '--' + mode], input=raw + b'\0' + bytecode,
                       capture_output=True, timeout=10)
    COUNT += 1
    assert p.returncode == status, (p.returncode, status, p.stderr, raw)
    return p.stdout.decode()


def check(m, b=b''):
    actual = run(m, b)
    assert actual == ref.main_hash(m, b), (actual, ref.main_hash(m, b))
    verified = dict(m, mainHash=actual)
    run(verified, b, 'verify-hash')
    run(dict(verified, mainHash=('1' if actual[0] != '1' else '0') + actual[1:]),
        b, 'verify-hash', 3)
    return actual


base = json.loads((VECTORS / 'empty.module.json').read_text())
bytecode = (VECTORS / 'empty.bytecode.bin').read_bytes()
expected = '72d3cefa18d306a696a64a3a825101ffa99277b90147763acfb8b8eea1fe641f'
assert check(base, bytecode) == expected
for stem, binary in [('math-1', 'empty'), ('math-2', 'empty'), ('two-versions', 'two-versions')]:
    m = json.loads((VECTORS / (stem + '.module.json')).read_text())
    b = (VECTORS / (binary + '.bytecode.bin')).read_bytes()
    assert check(m, b) == m['mainHash']
# Exercise SHA block and padding boundaries on many differently framed inputs.
for n in list(range(140)) + [255, 256, 257, 4096, 65536]:
    check(base, bytes(i % 256 for i in range(n)))
assert run(json.dumps(base, indent=4).encode(), bytecode) == expected
assert check(dict(base, dependencies=[], functions=[], externals={}, native=[]), bytecode) == expected
assert check(dict(base, mainHash='f'*64, description='different', authors=[{'name': 'Иван'}]), bytecode) == expected
for key, value in [('name', 'other'), ('version', '2')]:
    assert check(dict(base, **{key: value}), bytecode) != expected
for n in [0, len(bytecode)//2, len(bytecode)-1]:
    changed = bytearray(bytecode)
    changed[n] ^= 1
    assert check(base, bytes(changed)) != expected
    run(base, bytes(changed), 'verify-hash', 3)
assert check(base, bytecode + b'\0') != expected
assert check(base, bytecode[:-1]) != expected
m = dict(base, dependencies=[{'name': 'math', 'version': str(i), 'hash': str(i)*64} for i in (1, 2)])
h = check(m, bytecode)
assert check(dict(m, dependencies=list(reversed(m['dependencies']))), bytecode) == h
m['dependencies'][0]['hash'] = '3'*64
assert check(m, bytecode) != h
# Native variants, information and stored mainHash never enter the projection.
native = json.loads((VECTORS / 'native-description.json').read_text())
variant = dict(library='native/x.so', hash='f'*64, goatAbiVersion=1,
               target=native['target'], specializations=native['specializations'])
native = dict(base, functions=[dict(functionId=1, file=0, instruction=0,
                                   parameters=[dict(name='a'), dict(name='b')])], native=[variant])
h = check(native, bytecode)
without = copy.deepcopy(native)
without.pop('native', None)
assert check(without, bytecode) == h
# Fact normalization precedes hashing; significant facts remain in the hash.
f = dict(functionId=1, file=0, instruction=0, parameters=[],
         analysis=dict(formatVersion=1, summaries=[dict(when={}, result={'type': 'array'})]))
m = dict(base, functions=[f])
h = check(m, bytecode)
f['analysis']['summaries'][0]['result']['elements'] = {}
assert check(m, bytecode) == h
f['analysis']['summaries'][0]['result']['empty'] = True
assert check(m, bytecode) != h
for invalid in [dict(base, mainHash='A'*64), dict(base, mainHash='0'*63),
                dict(base, native=None), dict(base, unknown=1), dict(base, dependencies=None)]:
    run(invalid, bytecode, status=1)
    run(invalid, bytecode, 'verify-hash', 1)
run(b'{"formatVersion":1,"formatVersion":1}', status=1)
print(f'Module hash checks passed: {COUNT}')
