"""Independent module-v1 descriptor/guard/ABI fixtures, including a real DLL/SO call."""
import copy
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())
COUNT = 0


def run(mode, data=b'', *arguments):
    global COUNT
    COUNT += 1
    result = subprocess.run([EXE, mode, *map(str, arguments)], input=data,
                            capture_output=True, timeout=30)
    assert result.returncode == 0, (mode, result.returncode, result.stdout, result.stderr)
    return result.stdout.decode().strip()


def envelope(value, normalize=True):
    raw = ref.canonical(ref.normalize(value, 'native') if normalize else value)
    return b'GOATND01' + struct.pack('<Q', len(raw)) + raw + b'\0'


def check(value, expected=0, normalize=True):
    actual = int(run('descriptor', envelope(value, normalize)))
    assert actual == expected, (actual, expected, value)


def specialization(kind='integer', conditions=None, sid=1, fid=1):
    return dict(specializationId=sid, functionId=fid, symbol=f'goat_fn_{sid}_v1',
                signature=dict(id='goat.adapter.v1', arguments=[kind], result=kind),
                when=dict(argumentTypes=[kind], **({'conditions': conditions} if conditions else {})))


def predicate(kind, value, operation='equal'):
    return dict(argument=0, operation=operation, constant=dict(type=kind, value=value))


def descriptor(specs):
    return dict(copy.deepcopy(BASE), specializations=specs)


subprocess.run([EXE], check=True, timeout=30)
check(BASE)
blob = envelope(BASE)
for bad in (b'', blob[:8], blob[:-1], blob + b'\0', b'BADMAGIC' + blob[8:],
            blob[:-1] + b'X', blob[:8] + struct.pack('<Q', (1 << 64) - 1) + blob[16:],
            blob[:8] + struct.pack('<Q', len(blob)) + blob[16:],
            blob[:8] + struct.pack('<Q', 0) + blob[16:]):
    assert int(run('descriptor', bad)) == 2
for raw in (json.dumps(BASE).encode(), ref.canonical(BASE) + b'\n',
            b'\xef\xbb\xbf' + ref.canonical(BASE), b'{"formatVersion":1,"formatVersion":1}',
            b'{}', b'{"x":"\xff"}'):
    assert int(run('descriptor', b'GOATND01' + struct.pack('<Q', len(raw)) + raw + b'\0')) == 2
for abi in (2, 4294967295):
    check(dict(BASE, goatAbiVersion=abi), 3)
for field in BASE:
    bad = copy.deepcopy(BASE); del bad[field]
    check(bad, 2, False)
for field, value in (('formatVersion', 2), ('goatAbiVersion', 0), ('unknown', 1),
                     ('mainHash', 'x' * 64), ('module', '../module'), ('specializations', [])):
    check(dict(BASE, **{field: value}), 2, False)
for field, value in (('symbol', 'goat_fn_2_v1'), ('functionId', 0), ('functionId', 4294967296),
                     ('specializationId', 0), ('signature', {}), ('when', {})):
    bad = copy.deepcopy(BASE); bad['specializations'][0][field] = value
    actual = int(run('descriptor', envelope(bad, False)))
    assert actual in (2, 4), (field, actual)
check(descriptor([specialization(sid=4294967295, fid=4294967295)]))
check(descriptor([specialization(sid=1, fid=1), specialization(sid=3, fid=3)]))
check(descriptor([specialization(sid=1), specialization(sid=3)]), 4, False)
check(descriptor([specialization(sid=1), specialization(sid=1, fid=2)]), 2, False)
check(descriptor([specialization('integer'), specialization('real', sid=2)]))
check(dict(descriptor([specialization(sid=1), specialization(sid=3)]), goatAbiVersion=2), 4, False)
metadata = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
metadata.pop('functions', None)
metadata['native'] = [dict(library='native/test.so', hash='1' * 64, goatAbiVersion=1,
                           target=BASE['target'], specializations=BASE['specializations'])]
assert run('metadata', json.dumps(metadata).encode()) == '2'


# Every scalar type: exact matches, absence becomes null, extras do not add slots.
values = {'integer': [-2**63, 2**63-1, -1, 0, 1],
          'real': [struct.pack('>d', x).hex() for x in
                   (-math.inf, -0.0, 0.0, 1.0, math.inf, math.nan, math.nextafter(0, 1))],
          'character': ['\0', 'A', '\ud7ff', '\ue000', '\U0010ffff'],
          'boolean': [False, True], 'null': [None], 'string': ['', 'A\0😀', 'A\0😁']}
tags = {'null': 0, 'integer': 1, 'real': 2, 'string': 3, 'boolean': 4, 'character': 5}
for kind, constants in values.items():
    spec = specialization(kind)
    raw = json.dumps(descriptor([spec]), ensure_ascii=False).encode()
    payload = '0' if kind != 'string' else '1'
    assert run('match', raw, 1, tags[kind], payload) == '1'
    assert run('match', raw, 2, tags[kind], payload) == '1'
    assert run('match', raw, 0, tags[kind], payload) == ('1' if kind == 'null' else '0')
    assert run('match', raw, 1, (tags[kind] + 1) % 6, '0') == '0'
    for constant in constants:
        if kind == 'string':
            observed, bits = 'A\0😀', 1
        elif kind == 'real':
            observed = struct.unpack('>d', bytes.fromhex(constant))[0]
            bits = int(constant, 16)
        elif kind == 'character':
            observed, bits = constant, ord(constant)
        elif kind == 'integer':
            observed, bits = constant, constant & ((1 << 64) - 1)
        else:
            observed, bits = constant, int(constant or 0)
        expected_constant = struct.unpack('>d', bytes.fromhex(constant))[0] if kind == 'real' else constant
        for operation in ('equal', 'notEqual'):
            spec = specialization(kind, [predicate(kind, constant, operation)])
            raw = json.dumps(descriptor([spec]), ensure_ascii=False).encode()
            expected = observed == expected_constant
            if operation == 'notEqual':
                expected = not expected
            assert run('match', raw, 1, tags[kind], f'{bits:x}') == str(int(expected)), (kind, constant)

# Seeded differential checks use the independent executable format specification.
# Batch once: Windows process startup must not dominate the CI matrix.
rng = random.Random(209)
pairs = []
for kind, constants in values.items():
    operations = ['equal', 'notEqual']
    if kind in ('integer', 'real', 'character'):
        operations += ['less', 'lessEqual', 'greater', 'greaterEqual']
    for _ in range(100):
        pair = []
        for _ in range(2):
            pair.append(dict(argumentTypes=[kind], conditions=[
                predicate(kind, rng.choice(constants), rng.choice(operations))
                for _ in range(rng.randrange(6))]))
        pairs.append(pair)
actual = run('overlap', json.dumps(pairs, ensure_ascii=False).encode()).splitlines()
assert len(actual) == len(pairs)
for pair, got in zip(pairs, actual):
    assert bool(int(got)) == ref.guards_overlap(*pair), (pair, got)
COUNT += len(pairs)

# Compile the exact public C header into a separately loaded shared library.
# The fixture uses the process toolchain, including MINGW32, and host services.
compiler = os.environ.get('CC') or ('gcc' if os.name == 'nt' else 'cc')
macros = subprocess.check_output([compiler, '-include', 'stdlib.h', '-dM', '-E', '-x', 'c', '-'],
                                 input=b'', timeout=30).decode()
architecture = ('aarch64' if '__aarch64__' in macros else
                'x86_64' if '__x86_64__' in macros else 'x86')
actual_descriptor = copy.deepcopy(BASE)
actual_descriptor['target'].update(
    architecture=architecture, bits=32 if architecture == 'x86' else 64,
    os='windows' if os.name == 'nt' else 'linux',
    minimumOs=[10] if os.name == 'nt' else [0],
    cpuFeatures={'x86': ['i686'], 'x86_64': ['sse2'], 'aarch64': ['fp', 'neon']}[architecture],
    runtime=dict(family=('ucrt' if '#define _UCRT ' in macros else 'msvcrt')
                 if os.name == 'nt' else 'glibc', minimumVersion=[0]))
with tempfile.TemporaryDirectory(prefix='goat-module-abi-') as directory:
    directory = Path(directory)
    source = directory / 'fixture.c'
    library = directory / ('fixture.dll' if os.name == 'nt' else 'fixture.so')
    native = envelope(actual_descriptor)
    data = ','.join(str(b) for b in native)
    source.write_text('''#include <goat/module_abi.h>
#include <string.h>
GOAT_MODULE_EXPORT const unsigned char goat_module_descriptor_v1[] = {''' + data + '''};
GOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL goat_fn_1_v1(
    uint32_t version, const goat_module_context_v1 *ctx, uint32_t count,
    const goat_module_value_v1 *arguments, goat_module_value_v1 *result) {
    if (version != 1) return GOAT_MODULE_ABI_MISMATCH;
    if (!ctx || !ctx->services || ctx->services->struct_size < GOAT_MODULE_SERVICES_SIZE)
        return GOAT_MODULE_BAD_REQUEST;
    if (count != 2 || arguments[0].tag != GOAT_MODULE_INTEGER || arguments[1].tag != GOAT_MODULE_INTEGER)
        return GOAT_MODULE_TYPE_MISMATCH;
    if (result->tag || result->reserved || result->payload) return GOAT_MODULE_EXTERNAL_ERROR;
    const goat_module_services_v1 *s = ctx->services;
    if (!s->is_space(32) || s->is_space(65) || s->to_upper('a') != 'A' || s->to_lower('A') != 'a')
        return GOAT_MODULE_EXTERNAL_ERROR;
    uint64_t *temporary = s->allocate(ctx->state, sizeof(uint64_t));
    *temporary = arguments[0].payload + arguments[1].payload;
    result->tag = GOAT_MODULE_INTEGER;
    result->payload = *temporary;
    s->deallocate(ctx->state, temporary);
    return GOAT_MODULE_SUCCESS;
}
''')
    baseline = {'aarch64': '-march=armv8-a', 'x86_64': '-march=x86-64', 'x86': '-march=i686'}[architecture]
    subprocess.run([compiler, baseline, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', *([] if os.name == 'nt' else ['-fPIC']),
                    '-I', str(ROOT / 'include'), str(source), '-o', str(library)],
                   check=True, timeout=60)
    result = run('load', b'', library, len(native))
    assert 'returned 42' in result, result
    print(result)
print(f'Module ABI checks passed: {COUNT}; includes 600 seeded differential guard cases')
