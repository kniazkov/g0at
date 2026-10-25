#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Execute literal includes through source compilation, VM and saved module images."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

EXE = str(Path(sys.argv[1]).resolve())
COUNT = 0


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8'))
    return path


def invoke(mode, path, expected=None, output=None, cwd=None, fail=False):
    global COUNT
    COUNT += 1
    command = [EXE, mode, str(path)]
    if output is not None:
        command.append(str(output))
    result = subprocess.run(command, capture_output=True, timeout=40, cwd=cwd,
                            env=dict(os.environ, GOAT_MODULE_PATH='/never/search/includes'))
    assert (result.returncode != 0) if fail else (result.returncode == 0), (
        COUNT, command, result.returncode, result.stdout, result.stderr)
    if expected is not None:
        actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
        assert actual == expected, (COUNT, command, actual, expected, result.stderr)
    if fail:
        assert result.stderr, (COUNT, 'failure must have a diagnostic', command)
    return result


def metadata(artifact, source_root):
    with zipfile.ZipFile(artifact) as archive:
        raw = archive.read('module.json')
        wire = archive.read('bytecode.bin')
    info = json.loads(raw)
    assert str(source_root).encode() not in raw + wire, 'absolute build path leaked'
    assert str(source_root).replace('\\', '/').encode() not in raw + wire
    assert type(info['main']) is int and 0 <= info['main'] < len(info['files'])
    header = struct.unpack_from('<8s14I', wire)
    assert header[0] == b'GOATBC01' and len(info['files']) == header[6]
    for index, entry in enumerate(info['files']):
        assert set(entry) == {'name', 'external', 'entry'}
        assert type(entry['entry']) is int and 0 <= entry['entry'] <= 0xffffffff
        assert type(entry['external']) is bool
        assert not Path(entry['name']).is_absolute()
        if entry['external']:
            assert '/' not in entry['name'] and '\\' not in entry['name']
        file = struct.unpack_from('<4I', wire, header[11] + 16 * index)
        assert entry['entry'] == file[2] and file[0] <= file[2] < file[0] + file[1]
    functions = info.get('functions', [])
    assert len(functions) == header[7]
    for index, function in enumerate(functions):
        wire_function = struct.unpack_from('<8I', wire, header[12] + 32 * index)
        assert (function['functionId'], function['file'], function['instruction']) == wire_function[:3]
        assert 0 <= function['file'] < len(info['files'])
    return info


with tempfile.TemporaryDirectory(prefix='goat-source-include-') as temp:
    root = Path(temp)
    unrelated = root / 'unrelated'
    unrelated.mkdir()

    def case(name, files, expected, archive=False, descriptor=False):
        directory = root / name
        for filename, source in files.items():
            put(directory / filename, source)
        source = directory / 'main.goat'
        if descriptor:
            source = put(directory / 'app.gmod', json.dumps({
                'formatVersion': 1, 'name': 'include-tests', 'version': '1.0.0',
                'main': 'main.goat'}))
        invoke('run', source, expected, cwd=unrelated)
        if archive:
            artifact = root / (name + '.gbin')
            invoke('compile', source, '', output=artifact, cwd=unrelated)
            assert artifact.is_file() and artifact.stat().st_size
            info = metadata(artifact, root)
            if name == 'nested':
                assert [f['name'] for f in info['files']] == [
                    'main.goat', 'parts/a.goat', 'parts/deep/b.goat']
                assert not any(f['external'] for f in info['files'])
            if name == 'repeat':
                assert [f['file'] for f in info['functions']] == [1]
            if name == 'lexical':
                assert {f['file'] for f in info['functions']} == {0, 1}
            if name == 'outside':
                assert [(f['name'], f['external']) for f in info['files']] == [
                    ('main.goat', False), ('math.goat', True), ('math.goat', True)]
            shutil.rmtree(directory)
            invoke('binary', artifact, expected, cwd=unrelated)
        return directory

    case('nested', {
        'main.goat': 'println("main"); const a = include "parts/a.goat"; println(a.value);',
        'parts/a.goat': 'println("a"); const b = include "deep/b.goat"; public value = b.value + 1;',
        'parts/deep/b.goat': 'println("b"); public value = 41;'},
        'main\na\nb\n42\n', archive=True)
    case('repeat', {
        'main.goat': 'const a = include "a.goat"; const b = include "./a.goat"; '
                     'println(a == b); a.count += 1; println(b.count); println(a.next()); '
                     'println(b.count);',
        'a.goat': 'println("once"); public var count = 0; '
                  'public next = func() { count += 1; return count; };'},
        'once\ntrue\n1\n2\n2\n', archive=True, descriptor=True)
    case('lexical', {
        'main.goat': 'const secret = 81; const load = func() { var local = 91; '
                     'return include "a.goat"; }; const a = load(); '
                     'println(a.secretValue); println(a.localValue); println(a.answer());',
        'a.goat': 'public secretValue = secret; public localValue = local; '
                  'const hidden = 7; public answer = func() { return hidden; };'},
        'null\nnull\n7\n', archive=True)
    case('private', {
        'main.goat': 'const a = include "a.goat"; try { println(a.hidden); } '
                     'catch(e) { println("denied"); } println(a.visible);',
        'a.goat': 'const hidden = 1; public visible = 2;'}, 'denied\n2\n')
    case('lazy', {
        'main.goat': 'const deferred = func() { return include "a.goat"; }; '
                     'if(false) { include "never.goat"; } println("before"); '
                     'println(deferred().value); println(deferred().value);',
        'a.goat': 'println("loaded"); public value = 9;',
        'never.goat': 'println("MUST NOT RUN");'}, 'before\nloaded\n9\n9\n', archive=True)
    case('expression', {
        'main.goat': 'println(10 + (include "a.goat").value); '
                     'println((include "a.goat").twice(3));',
        'a.goat': 'public value = 4; public twice = func(x) { return 2 * x; };'},
        '14\n6\n', archive=True)
    case('arguments', {
        'main.goat': 'const combine = func(a, b, c) { return 100*a + 10*b + c; }; '
                     'println(combine(1, (include "a.goat").value, 3)); '
                     'println(combine((include "b.goat").value, 5, (include "a.goat").value));',
        'a.goat': 'public value = 4;', 'b.goat': 'public value = 2;'},
        '143\n254\n', archive=True)
    case('closure-include', {
        'main.goat': 'const a = include "parts/a.goat"; const closure = a.make(5); '
                     'println(closure(7)); println(closure(8));',
        'parts/a.goat': 'public make = func(x) { return func(y) { '
                        'const b = include "sub/b.goat"; return x + y + b.value; }; };',
        'parts/sub/b.goat': 'println("closure helper once"); public value = 30;'},
        'closure helper once\n42\n43\n', archive=True)
    case('failed-callback', {
        'main.goat': 'const a = include "a.goat"; println(a.call(func() { return 3; })); '
                     'try { a.call(func() { throw "callback"; }); } '
                     'catch(e) { println(e); } println(a.call(func() { return 4; }));',
        'a.goat': 'public call = func(f) { return f() + 10; };'},
        '13\ncallback\n14\n', archive=True)
    case('same-content', {
        'main.goat': 'const a = include "a.goat"; const b = include "b.goat"; '
                     'println(a == b); a.count = 4; println(b.count);',
        'a.goat': 'println("init"); public var count = 0;',
        'b.goat': 'println("init"); public var count = 0;'}, 'init\ninit\nfalse\n0\n')
    case('cycle-ready', {
        'main.goat': 'public ready = 12; const a = include "a.goat"; println(a.value);',
        'a.goat': 'const main = include "main.goat"; public value = main.ready + 1;'},
        '13\n', archive=True)
    case('cycle-early-caught', {
        'main.goat': 'const a = include "a.goat"; public later = 12; println(a.value);',
        'a.goat': 'const main = include "main.goat"; try { println(main.later); } '
                  'catch(e) { println("early"); } public value = 4;'}, 'early\n4\n')
    case('failed-repeated', {
        'main.goat': 'var first = null; try { include "bad.goat"; } catch(e) { '
                     'first = e; println(e.message); } '
                     'try { include "bad.goat"; } catch(e) { println(first == e); } '
                     'println("done");',
        'bad.goat': 'println("one effect"); throw { public message = "failure"; };'},
        'one effect\nfailure\ntrue\ndone\n', archive=True)
    case('finally', {
        'main.goat': 'try { include "bad.goat"; } catch(e) { println(e); } '
                     'finally { println("outer cleanup"); } println("after");',
        'bad.goat': 'try { throw "saved"; } finally { println("inner cleanup"); }'},
        'inner cleanup\nsaved\nouter cleanup\nafter\n', archive=True)
    case('throwing-function', {
        'main.goat': 'const a = include "a.goat"; try { a.fail(); } catch(e) { println(e); } '
                     'println((include "a.goat") == a);',
        'a.goat': 'println("once"); public fail = func() { throw "function error"; };'},
        'once\nfunction error\ntrue\n', archive=True)
    case('unicode', {
        'main.goat': 'println((include "директория/😀.goat").value);',
        'директория/😀.goat': 'public value = "готово";'}, 'готово\n', archive=True)
    case('empty', {'main.goat': 'const x = include "empty.goat"; '
                              'println(x == (include "empty.goat"));', 'empty.goat': ''}, 'true\n')

    # Every source is parsed before any user effects, including unexecuted includes.
    invalid = ['include path;', 'include ("a.goat");', 'include "a" + ".goat";',
               'include "";', 'include "a\\0.goat";', 'include 3;', 'include;',
               'if(false) { include path; }', 'const f = func() { include path; };',
               'if(false) { include "missing.goat"; }']
    for index, statement in enumerate(invalid):
        directory = root / f'invalid-{index}'
        source = put(directory / 'main.goat', 'println("MUST NOT RUN"); ' + statement)
        put(directory / 'a.goat', '')
        invoke('run', source, '', fail=True)
    directory = root / 'invalid-child'
    source = put(directory / 'main.goat', 'println("MUST NOT RUN"); if(false) { include "a.goat"; }')
    put(directory / 'a.goat', 'var broken = ;')
    invoke('run', source, '', fail=True)

    # External sources with equal basenames remain distinct, and their labels do not leak paths.
    external = root / 'external'
    put(external / 'a/math.goat', 'public value = 1;')
    put(external / 'b/math.goat', 'public value = 2;')
    case('outside', {
        'main.goat': 'const a = include "../external/a/math.goat"; '
                     'const b = include "../external/b/math.goat"; '
                     'println(a.value); println(b.value); println(a == b);'},
        '1\n2\nfalse\n', archive=True, descriptor=True)

    # Hard-link identities are canonical paths, not inode/file ID or source hashes.
    linked = root / 'hardlinks'
    a = put(linked / 'a/math.goat', 'println("math init"); const h = include "helper.goat"; '
                                  'public value = h.value; public var count = 0;')
    b = linked / 'b/math.goat'
    b.parent.mkdir(parents=True)
    os.link(a, b)
    put(a.parent / 'helper.goat', 'public value = "a";')
    put(b.parent / 'helper.goat', 'public value = "b";')
    main = put(linked / 'main.goat', 'const a = include "a/math.goat"; '
               'const b = include "b/math.goat"; println(a == b); '
               'println(a.value); println(b.value); a.count = 7; println(b.count);')
    expected = 'math init\nmath init\nfalse\na\nb\n0\n'
    invoke('run', main, expected, cwd=unrelated)
    artifact = root / 'hardlinks.gbin'
    invoke('compile', main, '', output=artifact)
    info = metadata(artifact, root)
    assert [f['name'] for f in info['files']] == [
        'main.goat', 'a/math.goat', 'a/helper.goat', 'b/math.goat', 'b/helper.goat']
    shutil.rmtree(linked)
    invoke('binary', artifact, expected)

    # Canonical source, not the symlink location, defines every relative include base.
    real = root / 'real'
    real_main = put(real / 'main.goat', 'const a = include "math.goat"; '
                    'const b = include "again.goat"; println(a == b); println(a.value);')
    real_math = put(real / 'math.goat', 'println("real once"); '
                    'public value = (include "helper.goat").value;')
    put(real / 'helper.goat', 'public value = "real helper";')
    aliases = root / 'aliases'
    aliases.mkdir()
    put(aliases / 'helper.goat', 'public value = "WRONG helper";')
    put(aliases / 'math.goat', 'println("WRONG math");')
    try:
        (real / 'again.goat').symlink_to(real_math)
        (aliases / 'main.goat').symlink_to(real_main)
        (aliases / 'linked-math.goat').symlink_to(real_math)
    except OSError as error:
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1314:
            raise
        print('SKIP symlink fixtures: Windows privilege not held')
    else:
        expected = 'real once\ntrue\nreal helper\n'
        invoke('run', aliases / 'main.goat', expected, cwd=unrelated)
        via = put(aliases / 'via.goat', 'const a = include "linked-math.goat"; '
                  'const b = include "../real/math.goat"; println(a == b); println(a.value);')
        invoke('run', via, expected, cwd=unrelated)
        artifact = root / 'symlinks.gbin'
        invoke('compile', aliases / 'main.goat', '', output=artifact)
        info = metadata(artifact, root)
        assert len(info['files']) == 3
        shutil.rmtree(real)
        shutil.rmtree(aliases)
        invoke('binary', artifact, expected, cwd=unrelated)

    # Function labels live in the existing bytecode string table, not JSON schema
    # extensions; nested/anonymous functions retain independent source-order IDs.
    diagnostic_root = root / 'diagnostic-mapping'
    main = put(diagnostic_root / 'main.goat',
               'const outer = func() {\n'
               '  const inner = func() {};\n'
               '  return func() { return 1; };\n'
               '};\n'
               'const alias = outer;\n'
               'const other = include "other.goat";\n')
    put(diagnostic_root / 'other.goat',
        'public outer = func() {\n  throw "diagnostic-payload";\n};\n' +
        ''.join(f'const f{i} = func() {{ return {i}; }};\n' for i in range(160)))
    artifact = root / 'diagnostic-mapping.gbin'
    invoke('compile', main, '', output=artifact)
    metadata(artifact, root)
    with zipfile.ZipFile(artifact) as archive:
        wire = archive.read('bytecode.bin')
    header = struct.unpack_from('<8s14I', wire)
    records = [struct.unpack_from('<8I', wire, header[12] + 32*i)
               for i in range(header[7])]

    def label(record):
        if record[6] == 0xffffffff:
            return None
        offset, size, kind, count = struct.unpack_from('<4I', wire,
                                                       header[10] + 16*record[6])
        assert kind == 1 and size == 4*(count + 1)
        return wire[header[14] + offset:header[14] + offset + 4*count].decode('utf-32-le')

    assert [(r[0], r[1], label(r)) for r in records[:4]] == [
        (1, 0, 'outer'), (2, 0, 'inner'), (3, 0, None), (4, 1, 'outer')]
    assert [label(r) for r in records[4:]] == [f'f{i}' for i in range(160)]
    locations = [struct.unpack_from('<6I', wire, header[13] + 24*i)
                 for i in range(header[8])]
    empty_body = records[1][3]
    empty_location = next(l for l in locations if l[0] <= empty_body < l[0] + l[1])
    assert empty_location[2:] == (0, 2, 17, 2), empty_location
    throw_locations = [l for l in locations if l[2] == 1 and l[5] == 4 and l[3] == 2]
    assert throw_locations and all(l[4] > 0 for l in throw_locations)
    # Relocation cannot change logical paths, source maps, names, or code identity.
    relocated = root / 'relocated-mapping'
    shutil.copytree(diagnostic_root, relocated)
    second = root / 'relocated-mapping.gbin'
    invoke('compile', relocated / 'main.goat', '', output=second, cwd=unrelated)
    assert artifact.read_bytes() == second.read_bytes()

    # A bounded deep chain exercises nested VM boundaries and local include indices.
    deep = root / 'deep'
    depth = 100
    for index in range(depth):
        child = f'const next = include "{index + 1}.goat"; public value = next.value + 1;' \
            if index + 1 < depth else 'public value = 1;'
        put(deep / f'{index}.goat', child)
    main = put(deep / 'main.goat', 'println((include "0.goat").value);')
    invoke('run', main, f'{depth}\n')
    artifact = root / 'deep.gbin'
    invoke('compile', main, '', output=artifact)
    info = metadata(artifact, root)
    assert len(info['files']) == depth + 1
    shutil.rmtree(deep)
    invoke('binary', artifact, f'{depth}\n')

print(f'{COUNT} source include execution fixtures passed')
