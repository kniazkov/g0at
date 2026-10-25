#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Shared archive fixtures for the compiler/loader/inspect/merge validation API."""
import copy
from module_interface_fixtures import with_external_declarations
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
TARGET = json.loads((VECTORS / 'native-description.json').read_text())['target']
COUNT = 0
OK, ARGUMENT, IO, LIMIT, MEMORY, CORRUPT = range(6)
INPUT, CONTAINER, BYTECODE, NATIVE = range(4)
PASSED, FAILED, SKIPPED = range(3)


def image():
    ins = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
           (7, 0, 0, 0), (50, 0, 0, 0)]
    functions = [(1, 0, 1, 4, 0, 0, 0xffffffff, 0)]
    locations = [(0, 4, 0, 1, 1, 0), (4, 2, 0, 2, 1, 1)]
    of = 64 + len(ins) * 8
    on = of + 16
    ol = on + 32
    end = ol + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(ins), 0, 1, 1, 2,
                        64, of, of, on, ol, end)
            + b''.join(struct.pack('<BBHI', *v) for v in ins)
            + struct.pack('<4I', 0, len(ins), 0, 0)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations))


IMAGE = image()
PAYLOAD = b'deliberately not an executable library'


def module(native=True):
    m = dict(copy.deepcopy(BASE), name='validation', version='1',
             functions=[dict(functionId=1, file=0, instruction=1, parameters=[])])
    if native:
        m['native'] = [dict(library='native/main.so', hash=hashlib.sha256(PAYLOAD).hexdigest(),
                           goatAbiVersion=1, target=copy.deepcopy(TARGET),
                           specializations=[dict(functionId=1, specializationId=1,
                               symbol='goat_fn_1_v1', signature=dict(id='goat.adapter.v1',
                               arguments=[], result='null'), when=dict(argumentTypes=[]))])]
    return m


def pack(parts):
    # Semantic validation must not require the producer's pinned zlib version.
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        for name, content in parts.items():
            z.writestr(name, content)
    return stream.getvalue()


def archive(m=None, code=IMAGE, payloads=None, rehash=True, raw_json=None):
    m = copy.deepcopy(m if m is not None else module())
    code = with_external_declarations(m, code)
    if rehash:
        m['mainHash'] = ref.main_hash(dict(m, native=[]), code)
    parts = {'module.json': raw_json if raw_json is not None else json.dumps(m).encode(),
             'bytecode.bin': code}
    parts.update(payloads if payloads is not None else
                 {v['library']: PAYLOAD for v in m.get('native', [])})
    return pack(parts)


def run(data=b'', status=OK, role='compiler', label='fixture.gbin', limits=None, env=None):
    global COUNT
    COUNT += 1
    args = [EXE, role, str(label)]
    if limits:
        args.extend(str(x) for x in limits)
    p = subprocess.run(args, input=data, capture_output=True, timeout=30, env=env)
    assert p.returncode == 0, (COUNT, p.returncode, p.stdout, p.stderr)
    lines = [json.loads(line) for line in p.stdout.splitlines()]
    result, events = lines[-1], lines[:-1]
    assert result['status'] == status, (COUNT, result, events, status)
    assert result['events'] == len(events)
    assert result['failures'] == sum(e['state'] == FAILED for e in events)
    assert result['skipped'] == sum(e['state'] == SKIPPED for e in events)
    assert all(e['archive'] == str(label) for e in events)
    if status == OK:
        assert not result['failures'] and not result['skipped']
        assert any(e['check'] == BYTECODE and e['state'] == PASSED for e in events)
        assert any(e['check'] == NATIVE and e['state'] == PASSED for e in events)
    return events


def failed(events, check=None, code=None):
    return [e for e in events if e['state'] == FAILED
            and (check is None or e['check'] == check)
            and (code is None or e['container'] == code)]


def skipped(events, check):
    return any(e['check'] == check and e['state'] == SKIPPED for e in events)


subprocess.run([EXE], check=True)
good = archive()
# The same C entry point/fixtures serve all prospective consumers, not duplicated validators.
for role in ('compiler', 'loader', 'inspect', 'merge'):
    baseline = run(good, role=role)
    assert run(good, role=role) == baseline
    run(archive(module(False)), role=role)
    bad = bytearray(IMAGE); bad[64] = 255
    assert failed(run(archive(code=bad), CORRUPT, role=role), BYTECODE)
    wrong = module(); wrong['functions'][0]['instruction'] = 2
    assert failed(run(archive(wrong), CORRUPT, role=role), BYTECODE)
    wrong = module(); wrong['native'][0]['specializations'][0]['symbol'] = 'goat_fn_2_v1'
    assert failed(run(archive(wrong), CORRUPT, role=role), NATIVE)

# Uninstalled external modules are graph-validation concerns, not archive validation.
m = module(False)
m['dependencies'] = [dict(name='missing', version='1', hash='0' * 64)]
m['externals'] = {'exported': dict(mutable=False, type='function',
                    functionRef=dict(module='missing', version='1', functionId=123))}
run(archive(m))
for dependencies in (None, []):
    m = module(False)
    if dependencies is not None:
        m['dependencies'] = dependencies
    run(archive(m))

# Two schema errors survive together, no invented hash or semantic failures.
m = module(); m['name'] = False; m['version'] = 42
schema = run(archive(m, rehash=False), CORRUPT)
assert {'/name', '/version'} <= {e['field'] for e in failed(schema)}, schema
assert skipped(schema, BYTECODE) and skipped(schema, NATIVE), schema
assert not failed(schema, code=12), schema
assert run(archive(m, rehash=False), CORRUPT) == schema
for raw in (b'{', b'{"name":1,', b'null'):
    events = run(archive(raw_json=raw), CORRUPT)
    assert len(failed(events)) == 1, events
    assert not failed(events, code=12), events
    assert skipped(events, BYTECODE) and skipped(events, NATIVE)

for data in (b'', b'not a ZIP', good[:20], good[:-20]):
    events = run(data, CORRUPT)
    assert skipped(events, BYTECODE) and skipped(events, NATIVE)

# CRC damage is caught before the damaged part can be trusted.
def break_crc(data, entry):
    data = bytearray(data)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        i = z.getinfo(entry)
        start = i.header_offset + 30 + len(i.filename.encode()) + len(i.extra)
    data[start] ^= 1
    return data


assert failed(run(break_crc(good, 'native/main.so'), CORRUPT), code=4)
bad_code = bytearray(IMAGE); bad_code[64] = 255
events = run(break_crc(archive(code=bad_code), 'native/main.so'), CORRUPT)
assert failed(events, code=4) and failed(events, BYTECODE), events
bad_metadata = module()
bad_metadata['native'][0]['specializations'][0]['symbol'] = 'goat_fn_2_v1'
events = run(break_crc(archive(bad_metadata), 'bytecode.bin'), CORRUPT)
assert failed(events, code=4) and failed(events, NATIVE), events
assert skipped(events, BYTECODE), events
assert any(e['check'] == BYTECODE and e['state'] == SKIPPED
           and e['prerequisite'] == CONTAINER and e['containerPrerequisite'] == 1
           for e in events), events

# Independent unreadable entries report separately, without derived schema/hash errors.
events = run(break_crc(break_crc(good, 'module.json'), 'bytecode.bin'), CORRUPT)
assert len(failed(events, code=4)) == 2, events
assert not failed(events, code=11) and not failed(events, code=12), events
assert skipped(events, BYTECODE) and skipped(events, NATIVE), events
# Undeclared payloads cannot yield a misleading successful native hash stage.
events = run(archive(module(False), payloads={'native/extra.so': PAYLOAD}), CORRUPT)
assert any(e['check'] == CONTAINER and e['detail'] == 4 and e['state'] == SKIPPED
           and e['containerPrerequisite'] == 3 for e in events), events
assert not any(e['check'] == CONTAINER and e['detail'] == 4 and e['state'] == PASSED
               for e in events), events

# Independent main/library hash failures both survive, while safe semantics still run.
m = module(); m['mainHash'] = '0' * 64; m['native'][0]['hash'] = '0' * 64
both = run(archive(m, rehash=False), CORRUPT)
assert failed(both, code=12) and failed(both, code=13), both
assert any(e['check'] == BYTECODE and e['state'] == PASSED for e in both), both
m = module(); m['mainHash'] = '0' * 64
assert failed(run(archive(m, rehash=False), CORRUPT), code=12)
assert failed(run(archive(payloads={'native/main.so': PAYLOAD + b'!'}), CORRUPT), code=13)

# Foreign/unknown requirements never suppress checking every record.
m = module()
v = copy.deepcopy(m['native'][0]); v['library'] = 'native/foreign.dll'
v['target'].update(os='windows', minimumOs=[10], runtime=dict(family='ucrt', minimumVersion=[0]))
v['goatAbiVersion'] = 2
m['native'].append(v)
run(archive(m))
m['native'][1]['specializations'][0]['symbol'] = 'goat_fn_99_v1'
assert failed(run(archive(m), CORRUPT), NATIVE)
m['native'][0]['specializations'][0]['symbol'] = 'goat_fn_98_v1'
assert len(failed(run(archive(m), CORRUPT), NATIVE)) == 2
m = module(); m['native'][0]['target']['cpuFeatures'] = ['future-capability']
run(archive(m))
m = module(); m['native'][0]['target']['cpuFeatures'] = ['neon']
assert failed(run(archive(m), CORRUPT), NATIVE)
m = module(); m['native'][0]['specializations'][0]['functionId'] = 55
run(archive(m), CORRUPT)

# Bounds, file errors and retained snapshots after input mutation/path replacement.
run(good, LIMIT, limits=(len(good) - 1, 1000000))
run(good, LIMIT, limits=(len(good), 1))
run(good, limits=(len(good), 1000000))
with tempfile.TemporaryDirectory(prefix='goat-validation-') as directory:
    directory = Path(directory)
    path = directory / 'модуль.gbin'
    for limits in (None, (len(good), 1000000)):
        path.write_bytes(good)
        run(role='file', label=path, limits=limits)
        assert path.read_bytes() == b'replacement'
    path.write_bytes(good)
    run(status=LIMIT, role='file', label=path, limits=(len(good) - 1, 1000000))
    run(status=IO, role='file', label=directory / 'missing.gbin')

    # An actual constructor is instrumented and positively proven executable,
    # then packaged. Neither valid nor damaged archive validation may run it.
    cc = shlex.split(os.environ.get('CC', 'gcc' if os.name == 'nt' else 'cc'))
    library = directory / ('probe.dll' if os.name == 'nt' else 'probe.so')
    source = directory / 'probe.c'
    source.write_text('#include <stdio.h>\n#include <stdlib.h>\n'
        '__attribute__((constructor)) static void probe(void) {\n'
        ' const char *p = getenv("GOAT_VALIDATION_MARKER");\n'
        ' if (p) { FILE *f = fopen(p, "wb"); if (f) { fputs("loaded", f); fclose(f); } }\n}\n')
    subprocess.run(cc + ['-shared'] + ([] if os.name == 'nt' else ['-fPIC'])
                   + [str(source), '-o', str(library)], check=True, capture_output=True)
    loader = directory / ('load.exe' if os.name == 'nt' else 'load')
    load_source = directory / 'load.c'
    load_source.write_text('#ifdef _WIN32\n#include <windows.h>\n'
        'int main(int n, char **a) { return n != 2 || !LoadLibraryA(a[1]); }\n'
        '#else\n#include <dlfcn.h>\n'
        'int main(int n, char **a) { return n != 2 || !dlopen(a[1], RTLD_NOW); }\n#endif\n')
    subprocess.run(cc + [str(load_source), '-o', str(loader)]
                   + ([] if os.name == 'nt' else ['-ldl']), check=True, capture_output=True)
    marker = directory / 'constructor-ran'
    env = dict(os.environ, GOAT_VALIDATION_MARKER=str(marker))
    subprocess.run([str(loader), str(library)], check=True, capture_output=True, env=env)
    assert marker.read_bytes() == b'loaded'
    marker.unlink()
    payload = library.read_bytes()
    m = module(); v = m['native'][0]
    name = 'native/probe.dll' if os.name == 'nt' else 'native/probe.so'
    v['library'] = name; v['hash'] = hashlib.sha256(payload).hexdigest()
    if os.name == 'nt':
        v['target'].update(os='windows', minimumOs=[10], runtime=dict(family='ucrt', minimumVersion=[0]))
    for role in ('compiler', 'loader', 'inspect', 'merge'):
        run(archive(m, payloads={name: payload}), role=role, env=env)
        assert not marker.exists()
        m_bad = copy.deepcopy(m); m_bad['mainHash'] = '0' * 64
        run(archive(m_bad, payloads={name: payload}, rehash=False), CORRUPT, role=role, env=env)
        assert not marker.exists()
print(f'{COUNT} shared module validation checks passed')
