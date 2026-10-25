#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compile literal imports and execute immutable verified module graphs (#195)."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

EXE = str(Path(sys.argv[1]).resolve())
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
from module_execution_support import ExecutionSuite, put


def info(path):
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read('module.json'))


with tempfile.TemporaryDirectory(prefix='goat-import-execution-') as temporary:
    suite = ExecutionSuite(EXE, temporary)
    unrelated = suite.unrelated
    Project = suite.project

    p = Project('identity-versions')
    for version in ['1', '2']:
        p.compile('counter', 'println("init ' + version + '"); external var count = 0; '
                  'external next = func() { count += 1; return count; };', version)
    artifact = p.run('const a = import "counter@1"; const b = import "counter@1"; '
                     'const c = import "counter@2"; println(a == b); println(a == c); '
                     'a.count = 4; println(b.next()); println(a.count); println(c.count);',
                     'init 1\ninit 2\ntrue\nfalse\n5\n5\n0\n')
    assert [(d['name'], d['version']) for d in info(artifact)['dependencies']] == [
        ('counter', '1'), ('counter', '2')]

    # Imported entry is metadata.main, not a hard-coded file zero or a named internal path.
    p = Project('nonzero-main')
    library = p.compile('library', 'println("WRONG file zero"); include "entry.goat";',
                        files={'entry.goat': 'println("actual entry"); external value = 42;'})
    with zipfile.ZipFile(library) as archive:
        entries = {name: archive.read(name) for name in archive.namelist()}
    metadata = json.loads(entries['module.json'])
    metadata['main'] = 1
    metadata['mainHash'] = ref.main_hash(metadata, entries['bytecode.bin'])
    entries['module.json'] = json.dumps(metadata).encode()
    with zipfile.ZipFile(library, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    p.run('println((import "library@1").value);', 'actual entry\n42\n')

    p = Project('transitive')
    p.compile('shared', 'println("shared once"); external var count = 0;')
    p.compile('left', 'const s = import "shared@1"; s.count += 1; external scope = s;')
    p.compile('right', 'const s = import "shared@1"; s.count += 1; external scope = s;')
    artifact = p.run('const a = import "left@1"; const b = import "right@1"; '
                     'println(a.scope == b.scope); println(a.scope.count);',
                     'shared once\ntrue\n2\n')
    assert [d['name'] for d in info(artifact)['dependencies']] == ['left', 'right']

    p = Project('defaults')
    p.compile('value', 'external value = 11;', '1')
    p.compile('value', 'external value = 22;', '2')
    p.compile('library', 'external value = (import "value").value;', defaults={'value': '1'})
    p.run('println((import "value").value); println((import "value@1").value); '
          'println((import "library@1").value);', '22\n11\n11\n', defaults={'value': '2'})

    p = Project('cross-module-context')
    p.compile('library', 'const secret = 7; public moduleOnly = 8; '
              'external answer = func() { return secret; }; '
              'external callback = func(f) { return f() + secret; }; '
              'external closure = func(x) { return func(y) { '
              'return x + y + (include "helper.goat").value; }; };',
              files={'helper.goat': 'println("helper once"); public value = 30;'})
    p.run('const secret = 90; const a = import "library@1"; println(a.answer()); '
          'println(a.callback(func() { return secret; })); '
          'try { println(a.secret); } catch(e) { println("private denied"); } '
          'try { println(a.moduleOnly); } catch(e) { println("public denied"); } '
          'try { a.extra = 1; } catch(e) { println("extension denied"); } '
          'const c = a.closure(5); println(c(7)); println(c(8)); '
          'try { a.callback(func() { throw "callback error"; }); } '
          'catch(e) { println(e); } println(a.answer());',
          '7\n97\nprivate denied\npublic denied\nextension denied\nhelper once\n42\n43\ncallback error\n7\n')

    p = Project('lexical-and-lazy')
    p.compile('library', 'println("loaded"); external callerSecret = secret; '
              'external callerLocal = local; external value = 9;')
    p.compile('never', 'println("MUST NOT RUN");')
    p.run('const secret = 81; const deferred = func() { var local = 91; '
          'return import "library@1"; }; if(false) { import "never@1"; } '
          'println("before"); const a = deferred(); println(a.callerSecret); '
          'println(a.callerLocal); println(a.value); println(deferred() == a);',
          'before\nloaded\nnull\nnull\n9\ntrue\n')

    p = Project('nested-initialization')
    p.compile('inner', 'println("inner"); external value = "inner string";')
    p.compile('outer', 'println("outer before"); const x = import "inner@1"; '
              'println(x.value); println("outer after"); external value = "outer string";')
    p.run('println("root before"); println((import "outer@1").value); println("root after");',
          'root before\nouter before\ninner\ninner string\nouter after\nouter string\nroot after\n')

    p = Project('expression-and-include')
    p.compile('library', 'external value = 4; external twice = func(x) { return 2*x; };')
    p.run('const combine = func(a,b,c) { return 100*a+10*b+c; }; '
          'println(10 + (import "library@1").value); '
          'println((import "library@1").twice(3)); '
          'println(combine(1, (import "library@1").value, 3)); '
          'println((include "helper.goat").value);', '14\n6\n143\n4\n',
          files={'helper.goat': 'public value = (import "library@1").value;'})

    p = Project('failed-repeat')
    p.compile('bad', 'println("one effect"); try { throw { external message = "failure"; }; } '
              'finally { println("inner cleanup"); }')
    p.run('var first = null; try { import "bad@1"; } catch(e) { first = e; println(e.message); } '
          'try { import "bad@1"; } catch(e) { println(first == e); } '
          'finally { println("outer cleanup"); } println("done");',
          'one effect\ninner cleanup\nfailure\ntrue\nouter cleanup\ndone\n')

    p = Project('uncaught-root-failure')
    p.compile('bad', 'println("once before failure"); throw "uncaught saved failure";')
    app = p.compile('app', 'import "bad@1";')
    p.invoke('binary', app, expected='once before failure\n', fail=True)

    p = Project('function-exception')
    p.compile('library', 'println("once"); external fail = func() { throw "function error"; };')
    p.run('const a = import "library@1"; try { a.fail(); } catch(e) { println(e); } '
          'println((import "library@1") == a);', 'once\nfunction error\ntrue\n')

    p = Project('callback-import-finally')
    p.compile('third', 'println("third init"); external value = "third value";')
    p.compile('library', 'external call = func(f) { try { println("library before"); '
              'return f(); } finally { println("library finally"); } };')
    p.run('const a = import "library@1"; println(a.call(func() { '
          'return (import "third@1").value; })); println("root after");',
          'library before\nthird init\nlibrary finally\nthird value\nroot after\n')

    p = Project('nested-failure')
    p.compile('inner', 'println("inner effect"); throw { external message = "nested failure"; };')
    p.compile('outer', 'println("outer effect"); import "inner@1";')
    p.run('var first = null; try { import "outer@1"; } catch(e) { first = e; println(e.message); } '
          'try { import "outer@1"; } catch(e) { println(first == e); } '
          'try { import "inner@1"; } catch(e) { println(first == e); }',
          'outer effect\ninner effect\nnested failure\ntrue\ntrue\n')

    # Static syntax collection rejects dynamic/internal/versionless operands even in dead code.
    invalid = ['import path;', 'import ("library@1");', 'import "library@1" + "";',
               'import "";', 'import 3;', 'import;', 'import "library";',
               'import "library@1/file.goat";', 'import "library/file.goat@1";',
               'import "../library@1";', 'import "library@1\\0";',
               'if(false) { import path; }', 'const f = func() { import "missing@1"; };',
               'if(false) { import "missing@1"; }']
    for index, code in enumerate(invalid):
        p = Project('invalid-' + str(index))
        p.compile('library', 'external value = 1;')
        p.compile('app', 'println("MUST NOT RUN"); ' + code, fail=True)

    p = Project('direct-source')
    p.compile('library', 'external value = 17;')
    source = put(p.root / 'main.goat', 'println((import "library@1").value);')
    app = p.root / 'app.gbin'
    p.invoke('compile', source, app)
    p.invoke('binary', app, expected='17\n')

    # The final named artifact, not only a synthetic source root, must be checked.
    p = Project('build-identity-cycle')
    p.compile('self', 'external value = 1;')
    source = p.root / 'sources/self-1/module.gmod'
    put(source.parent / 'main.goat', 'external value = (import "self@1").value;')
    output = p.root / 'not-published.gbin'
    p.invoke('compile', source, output, fail=True)
    assert not output.exists() and not Path(str(output) + '.pending.gbin').exists()

    # Every syntactic dependency is validated before effects, not merely reached imports.
    for kind in ['missing', 'corrupt', 'changed']:
        p = Project('preflight-' + kind)
        target = p.compile('library', 'println("library effect"); external value = 1;')
        app = p.compile('app', 'println("MUST NOT RUN"); if(false) { import "library@1"; }')
        if kind == 'missing':
            target.unlink()
        elif kind == 'corrupt':
            target.write_bytes(b'not an archive')
        else:
            target.unlink()
            p.compile('library', 'external value = 2;')
        p.invoke('binary', app, fail=True)

    # Capture a full graph before deleting every archive/source; runtime performs no reopening.
    p = Project('frozen')
    p.compile('inner', 'println("inner once"); external value = "снимок 😀";')
    p.compile('outer', 'external read = func() { return (import "inner@1").value; };')
    app = p.compile('app', 'const a = import "outer@1"; println(a.read()); println(a.read());')
    process = subprocess.Popen([EXE, 'frozen', str(app)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               cwd=unrelated, env=p.environment)
    try:
        assert process.stdout.readline().strip() == b'READY'
        shutil.rmtree(p.root)
        stdout, stderr = process.communicate(b'\n', timeout=60)
        assert process.returncode == 0, (process.returncode, stdout, stderr)
        assert stdout.decode('utf-8').replace('\r\n', '\n') == 'inner once\nснимок 😀\nснимок 😀\n'
        suite.count += 1
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

print(f'Module import execution checks passed: {suite.count}')
