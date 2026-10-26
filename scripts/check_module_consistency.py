#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Cross-part checks in every native mode, without opening any DLL/SO."""
import copy
from module_interface_fixtures import with_external_declarations
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
TARGET = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())['target']
COUNT = 0


def image():
    # Two retained functions with a gap: source identities do not depend on native candidates.
    ins = [(1, 0, 0, 8), (47, 0, 0, 0), (5, 0, 0, 0), (1, 0, 0, 10),
           (47, 0, 0, 0), (5, 0, 0, 0), (2, 0, 0, 0), (0, 0, 0, 0),
           (7, 0, 0, 0), (50, 0, 0, 0), (7, 0, 0, 0), (50, 0, 0, 0)]
    functions = [(1, 0, 1, 8, 0, 0, 0xffffffff, 0),
                 (0xffffffff, 0, 4, 10, 0, 0, 0xffffffff, 0)]
    locations = [(0, 8, 0, 1, 1, 0), (8, 2, 0, 2, 1, 1), (10, 2, 0, 3, 1, 0xffffffff)]
    of = 64 + len(ins) * 8
    on = of + 16
    ol = on + len(functions) * 32
    end = ol + len(locations) * 24
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(ins), 0, 1, 2, 3,
                        64, of, of, on, ol, end)
            + b''.join(struct.pack('<BBHI', *v) for v in ins)
            + struct.pack('<4I', 0, len(ins), 0, 0)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations))


IMAGE = image()
FUNCTIONS = [dict(functionId=1, file=0, instruction=1, parameters=[]),
             dict(functionId=0xffffffff, file=0, instruction=4, parameters=[])]


def spec(fid=1, sid=1):
    return dict(functionId=fid, specializationId=sid, symbol=f'goat_fn_{sid}_v1',
                signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                when=dict(argumentTypes=[]))


def module(name='app', version='1', native=True):
    m = dict(copy.deepcopy(BASE), name=name, version=version, functions=copy.deepcopy(FUNCTIONS))
    if native:
        m['native'] = [dict(library='native/main.so', hash=hashlib.sha256(b'not executable').hexdigest(),
                            goatAbiVersion=1, target=copy.deepcopy(TARGET), specializations=[spec()])]
    return m


def hashed(m, code=IMAGE):
    m = copy.deepcopy(m)
    code = with_external_declarations(m, code)
    projection = dict(m, native=[])
    m['mainHash'] = ref.main_hash(projection, code)
    return m


def archive(m, code=IMAGE, libraries=None, rehash=True):
    if rehash:
        m = hashed(m, code)
    code = with_external_declarations(m, code)
    if libraries is None:
        libraries = {v['library']: b'not executable' for v in m.get('native', [])}
    # These are semantic fixtures, not golden compressed bytes; host zlib is irrelevant.
    return ref.zip_bytes({'module.json': json.dumps(m, ensure_ascii=False).encode(),
                          'bytecode.bin': code, **libraries}, compressed=False)


def run(archives, expected=0, mode='off', descriptor=b'', index=0):
    global COUNT
    COUNT += 1
    data = struct.pack('<I', len(archives)) + b''.join(struct.pack('<I', len(a)) + a for a in archives)
    args = [EXE, mode] + ([str(index)] if mode in ('descriptor', 'graph') else [])
    p = subprocess.run(args, input=data + descriptor, capture_output=True, timeout=30)
    assert p.returncode == 0, (COUNT, p.returncode, p.stderr)
    assert int(p.stdout) == expected, (COUNT, int(p.stdout), expected, p.stderr)


def envelope(value):
    raw = ref.canonical(ref.normalize(value, 'native'))
    return b'GOATND01' + struct.pack('<Q', len(raw)) + raw + b'\0'


def descriptor(m, variant=0):
    m = hashed(m)
    v = m['native'][variant]
    return dict(formatVersion=1, module=m['name'], version=m['version'], mainHash=m['mainHash'],
                **{k: copy.deepcopy(v[k]) for k in ('target', 'goatAbiVersion', 'specializations')})


subprocess.run([EXE], check=True)
m = module()
for mode in ('off', 'auto', 'required'):
    run([archive(m)], mode=mode)
    run([archive(module(native=False))], mode=mode)
    # Hash every byte, even when no native execution is requested.
    run([archive(m, libraries={'native/main.so': b'not executable!'})], 113, mode)
    run([archive(m, libraries={'native/main.so': b'not executablE'})], 113, mode)
    run([archive(m, libraries={})], 110, mode)
    bad = copy.deepcopy(m)
    bad['functions'][0]['instruction'] = 2
    run([archive(bad)], 3, mode)
    bad = copy.deepcopy(m)
    bad['native'][0]['specializations'][0]['symbol'] = 'goat_fn_2_v1'
    run([archive(bad)], 4, mode)
    bad = copy.deepcopy(m)
    bad['native'][0]['specializations'].append(spec(sid=2))
    run([archive(bad)], 4, mode)
    bad = copy.deepcopy(m)
    bad['native'][0]['target']['cpuFeatures'] = ['neon']
    run([archive(bad)], 4, mode)
    bad = copy.deepcopy(m)
    bad['native'][0]['library'] = 'native/main.dll'
    run([archive(bad)], 4, mode)

# Unknown requirements and ABI are incompatible, not corrupt. All records still checked.
for key, value in [('goatAbiVersion', 2), ('goatAbiVersion', 0xffffffff)]:
    good = copy.deepcopy(m); good['native'][0][key] = value
    run([archive(good)])
good = copy.deepcopy(m); good['native'][0]['target']['cpuFeatures'] = ['future-cpu']
run([archive(good)])
for fid in (1, 0xffffffff):
    good = copy.deepcopy(m); good['native'][0]['specializations'] = [spec(fid)]
    run([archive(good)])
# No renumbering based on specialization subset or list position.
good = copy.deepcopy(m); good['native'][0]['specializations'] = [spec(0xffffffff), spec(1, 2)]
run([archive(good)])
# Foreign variants cannot disappear behind a host selection.
foreign = copy.deepcopy(m['native'][0])
foreign.update(library='native/foreign.dll', goatAbiVersion=2)
foreign['target'].update(os='windows', minimumOs=[10], runtime=dict(family='ucrt', minimumVersion=[0]))
good = copy.deepcopy(m); good['native'].append(foreign)
run([archive(good)])
for change in ('symbol', 'cpu'):
    bad = copy.deepcopy(good)
    if change == 'symbol':
        bad['native'][1]['specializations'][0]['symbol'] = 'goat_fn_99_v1'
    else:
        bad['native'][1]['target']['cpuFeatures'] = ['neon']
    run([archive(bad)], 4)

# Only a safely copied envelope is compared, never a library constructor/query.
d = descriptor(m)
run([archive(m)], mode='descriptor', descriptor=envelope(d))
for field, value in [('module', 'other'), ('version', '2'), ('mainHash', '0' * 64),
                     ('goatAbiVersion', 2)]:
    bad = copy.deepcopy(d); bad[field] = value
    run([archive(m)], 5, 'descriptor', envelope(bad))
for field, value in [('minimumOs', [999]), ('cpuFeatures', ['future-cpu'])]:
    bad = copy.deepcopy(d); bad['target'][field] = value
    run([archive(m)], 5, 'descriptor', envelope(bad))
for field, value in [('specializationId', 2), ('functionId', 0xffffffff)]:
    bad = copy.deepcopy(d); bad['specializations'][0][field] = value
    if field == 'specializationId':
        bad['specializations'][0]['symbol'] = 'goat_fn_2_v1'
    run([archive(m)], 5, 'descriptor', envelope(bad))
bad = copy.deepcopy(d); bad['specializations'][0]['signature']['result'] = 'integer'
run([archive(m)], 5, 'descriptor', envelope(bad))
run([archive(m)], 1, 'descriptor', envelope(d), index=99)
run([archive(m)], 5, 'descriptor', envelope(d)[:-1])
good = copy.deepcopy(m); good['native'][0]['cCompiler'] = dict(name='different', version='99')
run([archive(good)], mode='descriptor', descriptor=envelope(d))
good = copy.deepcopy(m); good['native'][0]['hash'] = hashlib.sha256(b'different').hexdigest()
run([archive(good, libraries={'native/main.so': b'different'})], mode='descriptor', descriptor=envelope(d))

# Hash-pinned owner-rooted graph, never a process-global numeric function table.
def dependency(m):
    return dict(name=m['name'], version=m['version'], hash=m['mainHash'])


def reference(m, fid=1):
    return {'exported': dict(mutable=False, type='function',
                             functionRef=dict(module=m['name'], version=m['version'], functionId=fid))}


c = hashed(module('c', native=False))
c2 = hashed(module('c', '2', native=False))
b = module('b', native=False); b['dependencies'] = [dependency(c)]; b['externals'] = reference(c)
b = hashed(b)
a = module('a', native=False); a['dependencies'] = [dependency(b)]; a['externals'] = reference(c)
a = hashed(a)
run([archive(a), archive(b), archive(c)], mode='graph')
run([archive(c), archive(a), archive(b), archive(c2)], mode='graph', index=1)
run([archive(a), archive(b)], 6, 'graph')
run([archive(a), archive(b), archive(c2)], 6, 'graph')
run([archive(a), archive(b), archive(c), archive(c)], 6, 'graph')
for fid in (1, 0xffffffff):
    good = copy.deepcopy(a); good['externals'] = reference(c, fid)
    run([archive(good), archive(b), archive(c)], mode='graph')
bad = copy.deepcopy(a); bad['externals'] = reference(c, 2)
run([archive(bad), archive(b), archive(c)], 7, 'graph')
bad = copy.deepcopy(a); bad['externals'] = reference(c2)
run([archive(bad), archive(b), archive(c), archive(c2)], 7, 'graph')
bad = copy.deepcopy(a); bad['dependencies'] = []; bad['externals'] = reference(c)
run([archive(bad), archive(b), archive(c)], 7, 'graph')
bad = copy.deepcopy(a); bad['dependencies'][0]['hash'] = '0' * 64
run([archive(bad), archive(b), archive(c)], 6, 'graph')
bad = copy.deepcopy(a); bad['externals'] = reference(a)
run([archive(bad), archive(b), archive(c)], 7, 'graph')
run([archive(a), archive(b), archive(c)], 1, 'graph', index=3)
# Nonzero normalized variant index is not the first ZIP entry.
multiple = copy.deepcopy(m)
second = copy.deepcopy(multiple['native'][0]); second['library'] = 'native/second.so'
second['specializations'] = [spec(0xffffffff)]
multiple['native'].append(second)
normalized = ref.normalize(hashed(multiple))
run([archive(multiple)], mode='descriptor', descriptor=envelope(descriptor(normalized, 1)), index=1)
run([archive(multiple)], 5, 'descriptor', envelope(descriptor(normalized, 0)), index=1)

# The existing standalone producer must preserve nested/anonymous identities
# across native selection. The standalone producer uses its existing query ABI;
# .gmod ABI-v1 native production remains #211, rather than being asserted here.
goat = Path(EXE).with_name('goat.exe' if Path(EXE).suffix == '.exe' else 'goat')
with tempfile.TemporaryDirectory(prefix='goat-consistency-') as directory:
    source = Path(directory) / 'identities.goat'
    source.write_text('const outer = func(x) { const same = func(y) { return y + 1; }; '
                      'return same(x); }; const same = func(x) { return x * 2; }; '
                      'const maker = func(value) { return func() { return value; }; }; '
                      'const unavailable = func(object) { return object.unknown; }; '
                      'println(outer(20) + same(10)); println(maker(3)()); println(unavailable);\n')
    tables = []
    original = source.read_text()
    for optimization, mode in [('none', 'off'), ('all', 'off'),
                               ('all', 'auto'), ('all', 'required')]:
        source = Path(directory) / f'{optimization}-{mode}.goat'
        source.write_text(original)
        subprocess.run([str(goat), '--compile', '--optimize', optimization, '--native', mode, str(source)],
                       check=True, capture_output=True, timeout=60)
        code = source.with_suffix('.gbin').read_bytes()
        if code[:8] == b'GOATPG01':
            code = code[96:]
        assert code[:8] == b'GOATBC01'
        count = struct.unpack_from('<I', code, 32)[0]
        offset = struct.unpack_from('<I', code, 52)[0]
        tables.append([struct.unpack_from('<8I', code, offset + i*32)[:6] for i in range(count)])
    assert all(table == tables[0] for table in tables) and len(tables[0]) == 6, tables
    assert [f[0] for f in tables[0]] == list(range(1, 7)), tables
    # A closure capturing a scalar and an uncalled object operation still own IDs
    # even though neither has a supported native implementation.
    generated = subprocess.run([str(goat), '--print-c', str(source)], check=True,
                               capture_output=True, timeout=60).stdout.decode('utf-8')
    assert 'g_f2_i_' in generated and 'g_f3_i_' in generated, generated
    assert 'g_f4_' not in generated and 'g_f5_' not in generated and 'g_f6_' not in generated
    COUNT += len(tables)
print(f'{COUNT} module consistency checks passed')
