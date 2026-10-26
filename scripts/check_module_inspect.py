#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Metadata-only goat-inspect integration, using independent portable fixtures."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

from module_interface_fixtures import with_external_declarations

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
TARGET = json.loads((VECTORS / 'native-description.json').read_text())['target']
COUNT = 0
CC = shutil.which(sys.argv[2] if len(sys.argv) > 2 else os.environ.get('CC', 'gcc'))
assert CC, 'A real C compiler is required to prove that constructors are not executed'


def image():
    instructions = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
                    (7, 0, 0, 0), (50, 0, 0, 0)]
    files = 64 + len(instructions) * 8
    functions = files + 16
    locations = functions + 32
    end = locations + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(instructions), 0, 1, 1, 2,
                        64, files, files, functions, locations, end)
            + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + struct.pack('<4I', 0, len(instructions), 0, 0)
            + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
            + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
            + struct.pack('<6I', 4, 2, 0, 2, 1, 1))


IMAGE = image()
PAYLOAD = b'This must never be passed to an OS library loader by metadata inspection.'


def metadata(name='inspect-sample', version='1'):
    return dict(copy.deepcopy(BASE), name=name, version=version,
                functions=[dict(functionId=1, file=0, instruction=1, parameters=[])])


def variant(target=None, library='native/test.so', abi=1):
    return dict(library=library, hash=hashlib.sha256(PAYLOAD).hexdigest(),
                goatAbiVersion=abi, target=copy.deepcopy(target or TARGET),
                cCompiler=dict(name='Compiler diagnostic', version='17.4 fixture'),
                specializations=[dict(functionId=1, specializationId=1,
                    symbol='goat_fn_1_v1', signature=dict(id='goat.adapter.v1',
                    arguments=[], result='null'), when=dict(argumentTypes=[]))])


def archive(doc=None, code=IMAGE, raw=None, rehash=True, payload=None):
    doc = copy.deepcopy(doc if doc is not None else metadata())
    code = with_external_declarations(doc, code)
    if rehash:
        doc['mainHash'] = ref.main_hash(doc, code)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', raw if raw is not None else
                   json.dumps(doc, ensure_ascii=False).encode('utf-8'))
        z.writestr('bytecode.bin', code)
        for native in doc.get('native', []):
            z.writestr(native['library'], PAYLOAD if payload is None else payload)
    return stream.getvalue(), dict(name=doc.get('name'), version=doc.get('version'),
                                  hash=doc.get('mainHash'))


def put(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content.encode('utf-8') if isinstance(content, str) else content)
    return path


with tempfile.TemporaryDirectory(prefix='goat-inspect-') as temporary:
    root = Path(temporary)
    cwd = root / 'unrelated'
    cwd.mkdir()
    base_env = dict(os.environ, GOAT_MODULE_PATH='')

    def run(*args, status=0, contains=(), absent=(), env=None):
        global COUNT
        current = dict(base_env)
        current.update(env or {})
        result = subprocess.run([EXE, *map(str, args)], cwd=cwd, env=current,
                                capture_output=True, timeout=30)
        COUNT += 1
        text = result.stdout.decode('utf-8').replace('\r\n', '\n')
        diagnostic = result.stderr.decode('utf-8').replace('\r\n', '\n')
        assert result.returncode == status, (COUNT, args, result.returncode, text, diagnostic)
        for item in contains:
            assert item in text + diagnostic, (COUNT, item, text, diagnostic)
        for item in absent:
            assert item not in text + diagnostic, (COUNT, item, text, diagnostic)
        if status:
            assert text or diagnostic, (COUNT, 'missing error diagnostic')
        assert '\x1b' not in text + diagnostic, (COUNT, 'raw terminal escape')
        return text, diagnostic

    binary, identity = archive()
    main = put(root / 'module.gbin', binary)
    before = main.read_bytes()
    baseline = run(main, contains=('inspect-sample', identity['hash']))
    assert run(main) == baseline, 'identical inspection must be deterministic'
    assert main.read_bytes() == before, 'inspection must not modify the archive'
    assert re.search(r'validation:\s*passed', baseline[0], re.I), baseline
    assert re.search(r'loading:\s*not checked', baseline[0], re.I), baseline

    # Literal paths never append a suffix and support unrelated/Unicode locations.
    for name in ['archive.other', 'extensionless', 'кириллица-😀/архив.bin']:
        run(put(root / name, binary), contains=(identity['hash'],))
    run(root / 'missing', status=1)
    put(root / 'missing.gbin', binary)
    run(root / 'missing', status=1, absent=('Module: "inspect-sample"',))
    put(root / 'literal', b'not an archive')
    put(root / 'literal.gbin', binary)
    run(root / 'literal', status=1)
    dash = put(cwd / '-archive', binary)
    run('--', dash.name, contains=(identity['hash'],))

    run('--help', contains=('goat-inspect',))
    run('--version', contains=('goat-inspect',))
    for args in [(), (main, main), ('--unknown', main), ('--no-probe', main),
                 ('--json', main), ('--module-dir',), ('--target',),
                 ('--module-dir', root, '--module-dir', cwd, main)]:
        run(*args, status=2)
    # Parameter failure precedes any attempt to open this missing archive/target.
    for value in ['0', '-1', '1.5', '1e2', ' 1', '+1', '1s', '', '9' * 80]:
        run(root / 'does-not-exist', '--probe-timeout', value, status=2)
    run(main, '--probe-timeout', status=2)
    for value in ['1', '5', '0005']:
        run(main, '--probe-timeout', value, contains=(identity['hash'],))

    # Informational strings preserve provenance, not compatibility or identity.
    doc = metadata()
    doc['producers'] = [dict(name='Z producer', version='1', buildId='build-z'),
                        dict(name='A producer', version='2', buildId='build-a'),
                        dict(name='A producer', version='1', buildId='build-b')]
    doc['externals'] = {'z-unknown': dict(mutable=True),
                        'a-known': dict(mutable=False, type='integer'),
                        'control\n\x1b[31m': dict(mutable=False)}
    information, details = archive(doc)
    put(main, information)
    shown = run(main, contains=('Z producer', 'A producer', 'build-z', 'build-a', 'build-b',
                               'a-known', 'z-unknown', 'integer', 'unknown'))[0]
    assert shown.index('a-known') < shown.index('control') < shown.index('z-unknown'), shown
    assert re.search(r'"a-known" mutable=false type="integer"', shown), shown
    assert re.search(r'"z-unknown" mutable=true type=unknown', shown), shown
    assert 'control\n\x1b' not in shown and ('\\n' in shown or '\\u000a' in shown), shown
    doc['producers'].reverse()
    doc['externals'] = dict(reversed(list(doc['externals'].items())))
    put(main, archive(doc)[0])
    assert run(main)[0] == shown, 'unordered metadata order must not affect report'

    # Canonical nested JSON also escapes DEL/C1, not only top-level strings.
    controls = metadata()
    controls['producers'] = [dict(name='producer\x7f', version='1', buildId='build\x9b31m\u202e\u2066')]
    put(main, archive(controls)[0])
    controls['description'] = 'line\u2028\u2029\u2069'
    put(main, archive(controls)[0])
    escaped = run(main)[0]
    assert '\x7f' not in escaped and '\x9b' not in escaped, escaped
    assert '\\u007f' in escaped and '\\u009b' in escaped, escaped
    for scalar in [0x2028, 0x2029, 0x202e, 0x2066, 0x2069]:
        assert chr(scalar) not in escaped and f'\\u{scalar:04x}' in escaped, escaped

    # Unknown export type is not invented; empty/missing exports are both valid.
    for exports in [None, {}]:
        doc = metadata()
        if exports is not None:
            doc['externals'] = exports
        put(main, archive(doc)[0])
        run(main, contains=('Exports:',))

    # Hash failures retain separately validated declared metadata, never success.
    doc = metadata('retained-metadata')
    doc['mainHash'] = '0' * 64
    put(main, archive(doc, rehash=False)[0])
    failed = run(main, status=1, contains=('retained-metadata', 'failed'))
    assert run(main, status=1) == failed
    doc = metadata('invalid-bytecode-metadata')
    code = bytearray(IMAGE)
    code[64] = 255
    put(main, archive(doc, code=code)[0])
    run(main, status=1, contains=('invalid-bytecode-metadata', 'failed'))
    doc = metadata('never-valid-metadata')
    doc['version'] = False
    doc['name'] = 42
    put(main, archive(doc, rehash=False)[0])
    bad_fields = run(main, status=1, contains=('/name', '/version'))
    assert 'Main hash:' not in bad_fields[0], bad_fields
    for raw in [b'{', b'null', b'{"name":1,']:
        put(main, archive(raw=raw)[0])
        report = run(main, status=1)
        assert 'Main hash:' not in report[0] and 'Module:' not in report[0], report
    for broken in [b'', b'not a ZIP', binary[:20], binary[:-20]]:
        put(main, broken)
        run(main, status=1)

    # Metadata inspection never invokes the OS loader, even for compatible opaque bytes.
    doc = metadata('native-metadata')
    doc['native'] = [variant()]
    put(main, archive(doc)[0])
    profile = put(root / 'target.json', json.dumps(TARGET))
    run(main, '--target', profile, contains=('native/test.so', 'Compiler diagnostic',
        '17.4 fixture', hashlib.sha256(PAYLOAD).hexdigest(), 'not checked'))
    foreign = copy.deepcopy(TARGET)
    foreign.update(os='windows', architecture='x86', bits=32, minimumOs=[10],
                   cpuFeatures=['i686'], runtime=dict(family='msvcrt', minimumVersion=[0]))
    foreign_profile = put(root / 'foreign.json', json.dumps(foreign))
    run(main, '--target', foreign_profile, contains=('incompatible',))
    doc['native'][0]['goatAbiVersion'] = 999
    put(main, archive(doc)[0])
    run(main, '--target', profile, contains=('incompatible', 'unsupported Goat native ABI'))
    doc['native'][0]['goatAbiVersion'] = 1
    doc['native'][0]['target']['cpuFeatures'].append('unknown-fixture-feature')
    put(main, archive(doc)[0])
    run(main, '--target', profile, contains=('incompatible', 'unknown required CPU feature'))
    doc['native'][0]['target'] = copy.deepcopy(TARGET)
    del doc['native'][0]['cCompiler']
    put(main, archive(doc)[0])
    run(main, '--target', profile, contains=('C compiler: unknown',))
    put(main, archive(doc, payload=b'changed library bytes')[0])
    run(main, '--target', profile, status=1, contains=('native-metadata', 'failed'))
    for invalid in ['{', '{}', json.dumps(dict(TARGET, unknown=True))]:
        put(profile, invalid)
        run(main, '--target', profile, status=2)

    # Closed informational schemas are still enforced by the shared validator.
    for information in [dict(producers=[{'name': 'x', 'version': '1', 'buildId': ''}]),
                        dict(producers=[{'name': 'x', 'version': '1', 'buildId': 'id'}] * 2)]:
        put(main, archive(dict(metadata(), **information), rehash=False)[0])
        run(main, status=1, absent=('Main hash:',))

    # Valid bytecode would loop forever if inspection accidentally executed it.
    looping = bytearray(IMAGE)
    struct.pack_into('<BBHI', looping, 64 + 3 * 8, 3, 0, 0, 3)
    put(main, archive(metadata('never-execute-bytecode'), code=looping)[0])
    run(main, contains=('never-execute-bytecode',))

    # A real constructor is observable, without executing any exported Goat function.
    source = put(root / 'constructor.c', r'''#include <stdio.h>
#include <stdlib.h>
static void mark(void) {
    const char *path = getenv("GOAT_INSPECT_MARKER");
    if (path) { FILE *f = fopen(path, "wb"); if (f) { fputs("loaded", f); fclose(f); } }
}
#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h; (void)reserved; if (reason == DLL_PROCESS_ATTACH) mark(); return TRUE;
}
#else
__attribute__((constructor)) static void initialize(void) { mark(); }
#endif
''')
    library = root / ('constructor.dll' if os.name == 'nt' else 'constructor.so')
    command = [CC, '-shared', str(source), '-o', str(library)]
    if os.name != 'nt':
        command.insert(1, '-fPIC')
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    assert compiled.returncode == 0, (command, compiled.stdout, compiled.stderr)
    marker = root / 'constructor-was-called'
    doc = metadata('never-load-constructor')
    target = copy.deepcopy(TARGET)
    if os.name == 'nt':
        target.update(os='windows', minimumOs=[10],
                      runtime=dict(family='msvcrt', minimumVersion=[0]))
    library_name = 'native/constructor' + library.suffix
    doc['native'] = [variant(target, library_name)]
    payload = library.read_bytes()
    doc['native'][0]['hash'] = hashlib.sha256(payload).hexdigest()
    put(main, archive(doc, payload=payload)[0])
    profile = put(root / 'constructor-target.json', json.dumps(target))
    run(main, '--target', profile, env={'GOAT_INSPECT_MARKER': str(marker)},
        contains=('never-load-constructor', 'not checked'))
    assert not marker.exists(), 'metadata inspection must not call native constructors'

    # Reachable graph: one validated shared node, versions, exact hashes and errors.
    dep_bytes, dep = archive(metadata('shared', '1'))
    leaf = put(root / 'modules/shared/1.gbin', dep_bytes)
    left_bytes, left = archive(dict(metadata('left'), dependencies=[dep]))
    right_bytes, right = archive(dict(metadata('right'), dependencies=[dep]))
    put(root / 'modules/left/1.gbin', left_bytes)
    put(root / 'modules/right/1.gbin', right_bytes)
    graph = dict(metadata('graph-root'), dependencies=[right, left])
    put(main, archive(graph)[0])
    first = run(main, contains=('graph-root', 'shared', 'left', 'right'))[0]
    assert len(re.findall(r'Module:\s*"shared"', first)) == 1, first
    graph['dependencies'].reverse()
    put(main, archive(graph)[0])
    assert run(main)[0] == first, 'dependency order cannot reorder the report'
    leaf.unlink()
    run(main, status=1, contains=('shared',))
    custom = root / 'explicit'
    put(custom / 'shared/1.gbin', dep_bytes)
    run(main, '--module-dir', custom, contains=('shared',))
    run(main, env={'GOAT_MODULE_PATH': str(custom)}, contains=('shared',))
    # First candidate remains authoritative, even if a later copy is correct.
    put(leaf, b'bad candidate')
    run(main, '--module-dir', custom, status=1)
    put(leaf, dep_bytes)
    graph['dependencies'][0]['hash'] = 'f' * 64
    put(main, archive(graph)[0])
    run(main, status=1, contains=('hash',))

    # Different versions remain distinct; functionRef resolution uses the full graph.
    version_two, dep_two = archive(metadata('shared', '2'))
    put(root / 'modules/shared/2.gbin', version_two)
    doc = dict(metadata('two-versions'), dependencies=[dep, dep_two])
    doc['externals'] = {'alias': dict(mutable=False, type='function',
        functionRef=dict(module='shared', version='2', functionId=1))}
    put(main, archive(doc)[0])
    report = run(main, contains=('alias',))[0]
    assert len(re.findall(r'Module:\s*"shared"', report)) == 2, report
    doc['externals']['alias']['functionRef']['functionId'] = 2
    put(main, archive(doc)[0])
    run(main, status=1, contains=('alias',))

    # Back edges to the named root are cycles, not an invitation to load a copy.
    cycle_b, b_identity = archive(dict(metadata('cycle-b'), dependencies=[
        dict(name='cycle-a', version='1', hash='0' * 64)]))
    put(root / 'modules/cycle-b/1.gbin', cycle_b)
    put(main, archive(dict(metadata('cycle-a'), dependencies=[b_identity]))[0])
    run(main, status=1, contains=('cycle-a', 'cycle-b'))

print(f'Module inspect checks passed: {COUNT}')
