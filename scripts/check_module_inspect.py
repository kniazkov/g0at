#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Text/JSON goat-inspect integration, using independent portable fixtures."""
import base64
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


def schema_check(item, spec, document, directory, path='$'):
    """Validate the published schema with the stdlib, including external refs."""
    if '$ref' in spec:
        filename, _, fragment = spec['$ref'].partition('#')
        if filename:
            document = json.loads((directory / filename).read_text(encoding='utf-8'))
        target = document
        for part in fragment.split('/')[1:]:
            target = target[part.replace('~1', '/').replace('~0', '~')]
        schema_check(item, target, document, directory, path)
    for requirement in spec.get('allOf', []):
        schema_check(item, requirement, document, directory, path)
    if 'if' in spec:
        try:
            schema_check(item, spec['if'], document, directory, path)
            matched = True
        except AssertionError:
            matched = False
        schema_check(item, spec.get('then' if matched else 'else', {}), document, directory, path)
    if 'anyOf' in spec:
        errors = []
        for choice in spec['anyOf']:
            try:
                schema_check(item, choice, document, directory, path)
                break
            except AssertionError as error:
                errors.append(str(error))
        else:
            raise AssertionError((path, 'anyOf', errors))
    if 'oneOf' in spec:
        matches = 0
        for choice in spec['oneOf']:
            try:
                schema_check(item, choice, document, directory, path)
                matches += 1
            except AssertionError:
                pass
        assert matches == 1, (path, 'oneOf', matches)
    kinds = {'null': lambda v: v is None, 'boolean': lambda v: type(v) is bool,
             'integer': lambda v: type(v) is int,
             'number': lambda v: type(v) in (int, float),
             'string': lambda v: isinstance(v, str),
             'array': lambda v: isinstance(v, list),
             'object': lambda v: isinstance(v, dict)}
    if 'type' in spec:
        types = spec['type'] if isinstance(spec['type'], list) else [spec['type']]
        assert any(kinds[k](item) for k in types), (path, types, item)
    if 'const' in spec:
        assert type(item) is type(spec['const']) and item == spec['const'], (path, item)
    if 'enum' in spec:
        assert any(type(item) is type(v) and item == v for v in spec['enum']), (path, item)
    if isinstance(item, dict):
        assert set(spec.get('required', [])) <= item.keys(), (path, 'required', item)
        properties = spec.get('properties', {})
        extra = spec.get('additionalProperties', True)
        for key, value in item.items():
            if key in properties:
                schema_check(value, properties[key], document, directory, path + '.' + key)
            elif extra is False:
                raise AssertionError((path, 'unknown property', key))
            elif isinstance(extra, dict):
                schema_check(value, extra, document, directory, path + '.' + key)
            if 'propertyNames' in spec:
                schema_check(key, spec['propertyNames'], document, directory, path)
    if isinstance(item, list):
        assert len(item) >= spec.get('minItems', 0), (path, item)
        assert len(item) <= spec.get('maxItems', len(item)), (path, item)
        if spec.get('uniqueItems'):
            assert len({json.dumps(x, sort_keys=True) for x in item}) == len(item), (path, item)
        for index, value in enumerate(item):
            schema_check(value, spec.get('items', {}), document, directory, f'{path}[{index}]')
    if isinstance(item, str):
        assert len(item) >= spec.get('minLength', 0), (path, item)
        assert len(item) <= spec.get('maxLength', len(item)), (path, item)
        if 'pattern' in spec:
            assert re.search(spec['pattern'], item), (path, item, spec['pattern'])
    if type(item) in (int, float):
        assert item >= spec.get('minimum', item), (path, item)
        assert item <= spec.get('maximum', item), (path, item)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, ('duplicate report key', key)
        result[key] = value
    return result


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
                 ('--module-dir',), ('--target',),
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

    # The machine report is a separate closed, versioned contract, not module.json.
    schema_path = ROOT / 'docs/module-format-v1/inspect-report.schema.json'
    report_schema = json.loads(schema_path.read_text(encoding='utf-8'))

    def run_json(*args, status=0, env=None, json_last=False):
        global COUNT
        current = dict(base_env)
        current.update(env or {})
        arguments = [arg if isinstance(arg, bytes) else str(arg) for arg in args]
        command = ([EXE, *arguments, '--json'] if json_last
                   else [EXE, '--json', *arguments])
        result = subprocess.run(command, cwd=cwd, env=current,
                                capture_output=True, timeout=30)
        COUNT += 1
        assert result.returncode == status, (COUNT, args, result.returncode,
                                             result.stdout, result.stderr)
        assert not result.stderr, (COUNT, 'JSON diagnostic leaked to stderr', result.stderr)
        text = result.stdout.decode('utf-8')
        report = json.loads(text, object_pairs_hook=unique_object)
        schema_check(report, report_schema, report_schema, schema_path.parent)
        assert report['reportVersion'] == 1 and type(report['reportVersion']) is int
        assert report['exitCode'] == status and report['success'] == (status == 0), report
        if status:
            assert any(d['severity'] == 'error' for d in report['diagnostics']), report
        for diagnostic in report['diagnostics']:
            if 'entryBase64' in diagnostic:
                decoded = base64.b64decode(diagnostic['entryBase64'], validate=True)
                assert base64.b64encode(decoded).decode('ascii') == diagnostic['entryBase64']
        for module in report['modules']:
            if module['metadata'] is None:
                assert module['exports'] is None and module['native'] is None, module
            else:
                assert [x['name'] for x in module['exports']] == sorted(
                    module['metadata'].get('externals', {})), module
                for entry in module['exports']:
                    source = module['metadata']['externals'][entry['name']]
                    assert entry['mutable'] == source['mutable'], entry
                    assert ('type' in entry) == ('type' in source), entry
                    if 'type' in source:
                        assert entry['type'] == source['type'], entry
                for native in module['native']:
                    assert set(native['compatibility']) == {'state'}, native
                    assert native['loading']['state'] == 'not_checked', native
                    assert set(native['loading']) == {'state', 'reason'}, native
        return report, text

    def codes(report, severity=None):
        return [d['code'] for d in report['diagnostics']
                if severity is None or d['severity'] == severity]

    def root_module(report):
        return next(m for m in report['modules'] if Path(m['archive']).resolve() == main.resolve())

    put(main, binary)
    success, serialized = run_json(main)
    assert run_json(main)[1] == serialized, 'JSON bytes must be deterministic'
    versioned = metadata(version='2026-special')
    put(main, archive(versioned)[0])
    version_report, _ = run_json(main)
    assert version_report['reportVersion'] == 1
    assert root_module(version_report)['metadata']['version'] == '2026-special'
    put(main, binary)
    current_target = success['target']
    assert current_target['source'] == 'current', current_target
    assert type(current_target['littleEndian']) is bool, current_target
    assert 'reportVersion' not in root_module(success)['metadata'], success
    assert root_module(success)['metadata']['mainHash'] == identity['hash'], success
    assert root_module(success)['validation'] == 'passed', success
    assert root_module(success)['exports'] == [] and root_module(success)['native'] == [], success
    assert 'NO_NATIVE_VARIANT' in codes(success, 'warning'), success
    assert main.read_bytes() == binary, 'JSON inspection changed input archive'

    for name in ['json.literal', 'json-no-extension', 'JSON-кириллица-😀/данные']:
        location = put(root / name, binary)
        report, _ = run_json(location)
        assert report['modules'][0]['metadata']['mainHash'] == identity['hash'], report
    put(cwd / '--json', binary)
    run_json('--', '--json')
    missing, _ = run_json(root / 'absent-json', status=1)
    assert 'ARCHIVE_IO' in codes(missing), missing
    put(root / 'absent-json.gbin', binary)
    assert run_json(root / 'absent-json', status=1)[0] == missing
    assert not missing['modules'], missing

    for args in [(), (main, main), ('--unknown', main), ('--no-probe', main),
                 ('--module-dir',), ('--target',), ('--probe-timeout', '0', main)]:
        report, _ = run_json(*args, status=2)
        assert 'INVALID_ARGUMENT' in codes(report), report
        assert not report['modules'], 'argument error opened archive'
    report, _ = run_json('--unknown', main, status=2, json_last=True)
    assert 'INVALID_ARGUMENT' in codes(report), report
    if os.name != 'nt':
        for last in (False, True):
            report, _ = run_json(b'invalid-utf8-\xff', status=2, json_last=last)
            assert codes(report, 'error') == ['INVALID_ARGUMENT'], report
            assert report['input'] is None and not report['modules'], report
    if os.name != 'nt':
        report, _ = run_json(main, status=1, env={'GOAT_MODULE_PATH': b'invalid-utf8-\xff'})
        assert codes(report, 'error') == ['ARCHIVE_IO'], report
        assert not report['modules'], report
    for flag in ['--help', '--version']:
        report, _ = run_json(flag)
        assert report['information']['kind'] == flag[2:], report
        assert report['information']['text'] and not report['modules'], report
    report, _ = run_json(main, '--target', root / 'missing-target', status=2)
    assert 'TARGET_IO' in codes(report), report
    for value in ['{', '{}', json.dumps(dict(TARGET, illegal=True))]:
        invalid_profile = put(root / 'invalid-target.json', value)
        report, _ = run_json(main, '--target', invalid_profile, status=2)
        assert 'TARGET_INVALID' in codes(report) and not report['modules'], report

    # All optional information survives exactly; empties normalize, never trim.
    doc = metadata('information-json')
    doc.update(description='Описание\n\x1b\u009b 😀', license='MIT', homepage='https://example.test/',
               authors=[{'name': 'Имя 😀', 'email': ''}, {'name': 'Second', 'email': 'x@y.test'}],
               producers=[{'name': 'Z', 'version': '3', 'buildId': 'zzz'},
                          {'name': 'A', 'version': '1', 'buildId': 'aaa'}],
               externals={'unknown': {'mutable': True},
                          'known': {'mutable': False, 'type': 'integer'},
                          'control\n\x1b': {'mutable': False}})
    put(main, archive(doc)[0])
    report, serialized = run_json(main)
    retained = root_module(report)['metadata']
    for field in ['description', 'license', 'homepage']:
        assert retained[field] == doc[field], (field, retained)
    assert retained['authors'] == [{'name': 'Имя 😀'}, {'name': 'Second', 'email': 'x@y.test'}]
    assert sorted(retained['producers'], key=lambda p: p['name']) == sorted(
        doc['producers'], key=lambda p: p['name']), retained
    assert '\x1b' not in serialized, 'raw ASCII terminal escape in JSON'
    doc['producers'].reverse()
    doc['externals'] = dict(reversed(list(doc['externals'].items())))
    put(main, archive(doc)[0])
    assert run_json(main)[1] == serialized, 'unordered metadata reordered JSON report'
    for field, empty in [('description', ''), ('license', ''), ('homepage', ''), ('authors', [])]:
        empty_doc = dict(metadata(), **{field: empty})
        put(main, archive(empty_doc)[0])
        empty_report, _ = run_json(main)
        assert field not in root_module(empty_report)['metadata'], empty_report
    large = dict(metadata(), externals={'exact': dict(mutable=False, type='integer',
                                                     value=9007199254740993)})
    put(main, archive(large)[0])
    large_report, _ = run_json(main)
    assert root_module(large_report)['metadata']['externals']['exact']['value'] == 9007199254740993
    spaces = dict(metadata(), description=' ', authors=[{'name': 'N', 'email': ' '}])
    put(main, archive(spaces)[0])
    spaces_report, _ = run_json(main)
    assert root_module(spaces_report)['metadata']['description'] == ' '
    assert root_module(spaces_report)['metadata']['authors'][0]['email'] == ' '
    for invalid in [dict(description=None), dict(authors=[{'name': ''}]),
                    dict(authors=[{'name': 'N', 'email': None}])]:
        put(main, archive(dict(metadata(), **invalid), rehash=False)[0])
        invalid_report, _ = run_json(main, status=1)
        assert root_module(invalid_report)['metadata'] is None, invalid_report

    # Failed independent checks and skipped prerequisites remain distinguishable.
    bad = dict(metadata('retained-json'), mainHash='0' * 64)
    put(main, archive(bad, rehash=False)[0])
    retained_report, stable = run_json(main, status=1)
    assert 'MAIN_HASH_MISMATCH' in codes(retained_report), retained_report
    assert root_module(retained_report)['metadata']['name'] == 'retained-json'
    assert root_module(retained_report)['validation'] == 'failed'
    assert run_json(main, status=1)[1] == stable
    english = codes(run_json(main, status=1, env={'LANG': 'C', 'LC_ALL': 'C'})[0])
    russian = codes(run_json(main, status=1, env={'LANG': 'ru_RU.UTF-8', 'LC_ALL': 'ru_RU.UTF-8'})[0])
    assert english == russian, 'stable diagnostic code depended on message locale'
    bad = dict(metadata(), name=1, version=False)
    put(main, archive(bad, rehash=False)[0])
    fields, _ = run_json(main, status=1)
    assert {'/name', '/version'} <= {d.get('field') for d in fields['diagnostics']}, fields
    assert sum(d['severity'] == 'error' for d in fields['diagnostics']) >= 2, fields
    for raw in [b'{', b'{"name":1,']:
        put(main, archive(raw=raw)[0])
        broken, _ = run_json(main, status=1)
        assert 'JSON_INVALID' in codes(broken), broken
        assert 'MAIN_HASH_MISMATCH' not in codes(broken), broken
        assert root_module(broken)['metadata'] is None, broken
        skipped = [d for d in broken['diagnostics'] if d['severity'] == 'skipped']
        assert skipped and all('prerequisite' in d for d in skipped), broken
    for raw in [b'', b'garbage', binary[:20], binary[:-20]]:
        put(main, raw)
        broken, _ = run_json(main, status=1)
        assert any(c in codes(broken) for c in ['ZIP_INVALID', 'CONTAINER_INVALID']), broken
        assert not any(m['metadata'] is not None for m in broken['modules']), broken
        assert 'MAIN_HASH_MISMATCH' not in codes(broken), broken

    # Compatibility is not loading, and JSON has no text-only mismatch-reason list.
    profile = put(root / 'json-target.json', json.dumps(TARGET))
    doc = dict(metadata('json-native'), native=[variant(), variant(library='native/other.so', abi=999)])
    put(main, archive(doc)[0])
    native_report, _ = run_json(main, '--target', profile)
    assert native_report['target']['source'] == 'explicit'
    assert native_report['target']['os'] == TARGET['os']
    assert native_report['target']['architecture'] == TARGET['architecture']
    assert native_report['target']['bits'] == TARGET['bits']
    native_entries = {n['library']: n for n in root_module(native_report)['native']}
    assert native_entries['native/test.so']['compatibility']['state'] == 'compatible'
    assert native_entries['native/other.so']['compatibility']['state'] == 'incompatible'
    assert len(root_module(native_report)['metadata']['native']) == 2
    assert root_module(native_report)['metadata']['native'][0]['cCompiler']['version'] == '17.4 fixture'
    for entry in doc['native']:
        del entry['cCompiler']
    put(main, archive(doc)[0])
    no_compiler, _ = run_json(main, '--target', profile)
    assert all('cCompiler' not in n for n in root_module(no_compiler)['metadata']['native'])
    put(main, archive(doc, payload=b'corrupt native')[0])
    failed_native, _ = run_json(main, '--target', profile, status=1)
    assert 'NATIVE_HASH_MISMATCH' in codes(failed_native), failed_native
    assert all(n['compatibility']['state'] == 'not_checked'
               for n in root_module(failed_native)['native']), failed_native

    put(main, archive(metadata('json-no-execution'), code=looping)[0])
    run_json(main)
    doc = metadata('json-no-constructor')
    doc['native'] = [variant(target, library_name)]
    doc['native'][0]['hash'] = hashlib.sha256(payload).hexdigest()
    put(main, archive(doc, payload=payload)[0])
    profile = put(root / 'json-constructor-target.json', json.dumps(target))
    run_json(main, '--target', profile, env={'GOAT_INSPECT_MARKER': str(marker)})
    assert not marker.exists(), 'JSON inspection called native constructor'
    doc['native'][0]['hash'] = '0' * 64
    put(main, archive(doc, payload=payload)[0])
    run_json(main, '--target', profile, status=1, env={'GOAT_INSPECT_MARKER': str(marker)})
    assert not marker.exists(), 'invalid archive caused native constructor execution'

    # Graph shares one node but keeps every path to an error deterministically.
    put(leaf, dep_bytes)
    _, json_left = archive(dict(metadata('left'), dependencies=[dep]))
    _, json_right = archive(dict(metadata('right'), dependencies=[dep]))
    graph = dict(metadata('json-graph'), dependencies=[json_right, json_left])
    put(main, archive(graph)[0])
    shared_report, stable = run_json(main)
    names = [m['metadata']['name'] for m in shared_report['modules']]
    assert names == sorted(names) and names.count('shared') == 1, names
    graph['dependencies'].reverse()
    put(main, archive(graph)[0])
    assert run_json(main)[1] == stable
    leaf.unlink()
    missing_dep, stable = run_json(main, status=1)
    assert 'DEPENDENCY_NOT_FOUND' in codes(missing_dep), missing_dep
    paths = [d['path'] for d in missing_dep['diagnostics'] if d['code'] == 'DEPENDENCY_NOT_FOUND']
    assert len(paths) == 2 and paths[0] != paths[1], missing_dep
    assert run_json(main, status=1)[1] == stable
    put(leaf, dep_bytes)
    graph['dependencies'][0]['hash'] = 'f' * 64
    put(main, archive(graph)[0])
    hash_report, _ = run_json(main, status=1)
    assert 'DEPENDENCY_HASH_MISMATCH' in codes(hash_report), hash_report
    put(main, archive(dict(metadata('cycle-a'), dependencies=[b_identity]))[0])
    cycle_report, _ = run_json(main, status=1)
    assert 'DEPENDENCY_CYCLE' in codes(cycle_report), cycle_report
    doc = dict(metadata('json-reference'), dependencies=[dep, dep_two],
               externals={'alias': dict(mutable=False, type='function',
                   functionRef=dict(module='shared', version='2', functionId=2))})
    put(main, archive(doc)[0])
    reference_report, _ = run_json(main, status=1)
    assert 'REFERENCE_INVALID' in codes(reference_report), reference_report

    # Missing/invalid edges suppress reference resolution without inventing errors.
    doc['externals']['alias']['functionRef']['functionId'] = 1
    for broken_edge in ('missing', 'hash'):
        doc['dependencies'] = [dep, copy.deepcopy(dep_two)]
        if broken_edge == 'missing':
            (root / 'modules/shared/2.gbin').unlink()
        else:
            put(root / 'modules/shared/2.gbin', version_two)
            doc['dependencies'][1]['hash'] = 'f' * 64
        put(main, archive(doc)[0])
        skipped_report, _ = run_json(main, status=1)
        skipped = [d for d in skipped_report['diagnostics']
                   if d['code'] == 'REFERENCE_SKIPPED']
        assert skipped and all(d['severity'] == 'skipped' and
            d['check'] == 'references' and 'prerequisite' not in d for d in skipped), skipped_report
        assert 'REFERENCE_INVALID' not in codes(skipped_report), skipped_report
        assert ('DEPENDENCY_NOT_FOUND' if broken_edge == 'missing' else
                'DEPENDENCY_HASH_MISMATCH') in codes(skipped_report), skipped_report

print(f'Module inspect checks passed: {COUNT}')
