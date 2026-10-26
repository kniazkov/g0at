#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Producer interfaces and metadata-only consumer contracts (#205, #231)."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import zipfile
from module_execution_support import ExecutionSuite

EXE = str(Path(sys.argv[1]).resolve())


def metadata(path):
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read('module.json'))


def exports(path):
    return metadata(path).get('externals', {})


with tempfile.TemporaryDirectory(prefix='goat-module-interface-') as temporary:
    suite = ExecutionSuite(EXE, temporary)
    p = suite.project('scalars')
    artifact = p.compile('scalars', '''
        println("initializer");
        external exact = 9007199254740993;
        external minimum = -9223372036854775807 - 1;
        external maximum = 9223372036854775807;
        external text = " a\\0b "; external blank = "";
        external no = false; external yes = true; external nothing = null;
        external real = 1.5; external letter = 'Я';
        const plus = 0.0; const minus = -0.0;
        external plusZero = plus; external minusZero = minus;
        external var mutable = 42;
        public hidden = 9; const secret = 7;
        external dynamic = readln();
    ''')
    fields = exports(artifact)
    for name, kind, value in [('exact', 'integer', 9007199254740993),
                              ('minimum', 'integer', -(2**63)),
                              ('maximum', 'integer', 2**63-1),
                              ('text', 'string', ' a\0b '), ('blank', 'string', ''),
                              ('no', 'boolean', False), ('yes', 'boolean', True),
                              ('real', 'real', '3ff8000000000000'),
                              ('plusZero', 'real', '0000000000000000'),
                              ('minusZero', 'real', '8000000000000000'),
                              ('letter', 'character', 'Я')]:
        assert fields[name] == {'mutable': False, 'type': kind, 'value': value}, (name, fields)
    assert fields['nothing'] == {'mutable': False, 'type': 'null'}
    assert fields['mutable'] == {'mutable': True}
    assert 'hidden' not in fields and 'secret' not in fields
    assert 'value' not in fields['dynamic']

    p = suite.project('function-identities')
    artifact = p.compile('identity', '''
        external factory = func(x) { return func() { return x; }; };
        external again = factory;
        external sibling = func(value) { return value; };
        external var replaceable = sibling;
        include "other.goat";
        include "last.goat";
    ''', files={'other.goat': 'const sibling = func() { return 9; }; sibling();',
               'last.goat': 'const factory = func(x) { return func() { return x + 1; }; }; '
                            'factory(8)();'})
    doc = metadata(artifact)
    assert [f['functionId'] for f in doc['functions']] == [1, 2, 3, 4, 5, 6], doc
    assert [f['file'] for f in doc['functions']] == [0, 0, 0, 1, 2, 2], doc
    assert doc['externals']['factory']['functionId'] == 1
    assert doc['externals']['again']['functionId'] == 1
    assert doc['externals']['sibling']['functionId'] == 3
    assert doc['externals']['replaceable'] == {'mutable': True}
    assert [f['parameters'] for f in doc['functions']] == [
        [{'name': 'x'}], [], [{'name': 'value'}], [], [{'name': 'x'}], []]
    p.run('const m = import "identity@1"; const a = m.factory(11); '
          'const b = m.factory(22); println(a()); println(b()); '
          'm.replaceable = func(x) { return "replacement"; }; println(m.replaceable(3));',
          '11\n22\nreplacement\n')

    p = suite.project('metadata-consumer')
    library = p.compile('library', '''
        println("library initialized");
        external answer = 9007199254740993;
        external identity = func(value) { return value; };
        identity(1); identity(1.5);
        external var changing = 1;
    ''')
    info = metadata(library)
    function = next(f for f in info['functions'] if f['functionId'] == info['externals']['identity']['functionId'])
    assert function.get('analysis', {}).get('summaries'), function
    # Only the immutable archive is available; compilation must not run initialization.
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.compile('app', '''
        const m = import "library@1";
        external exact = m.answer;
        external result = m.identity(5);
        external unknown = m.changing;
        println(exact); println(result);
    ''')
    fields = exports(app)
    assert fields['exact'] == {'mutable': False, 'type': 'integer', 'value': 9007199254740993}, fields
    assert fields['result'].get('type') == 'integer', fields
    assert fields['unknown'] == {'mutable': False}, fields
    p.invoke('binary', app, expected='library initialized\n9007199254740993\n5\n')

    p = suite.project('reexports')
    for version, result in [('1', 11), ('2', 22)]:
        p.compile('owner', 'println("owner ' + version + '"); '
                  'external answer = func() { return ' + str(result) + '; }; answer();', version)
    middle = p.compile('middle', 'println("middle"); external answer = (import "owner@1").answer;')
    top = p.compile('top', 'println("top"); external answer = (import "middle@1").answer;')
    for archive in [middle, top]:
        field = exports(archive)['answer']
        assert field.get('functionRef') == {'module': 'owner', 'version': '1', 'functionId': 1}, field
        assert 'functionId' not in field
    assert [(d['name'], d['version']) for d in metadata(top)['dependencies']] == [('middle', '1')]
    p.run('const m = import "top@1"; const n = import "owner@2"; '
          'println(m.answer()); println(n.answer());', 'top\nmiddle\nowner 1\nowner 2\n11\n22\n')

    p = suite.project('proof-boundaries')
    artifact = p.compile('proofs', '''
        external factory = func(x) { return func() { return x; }; };
        const a = factory(1); a(); const b = factory("str"); b();
        external callback = func(cb) { return cb(); };
        callback(func() { return 3; });
        external uncertain = func() { try { return 4; } catch(e) { return 5; } };
        uncertain();
        { external nested = 9; }
        include "included.goat";
        throw "stop";
        external later = readln();
    ''', files={'included.goat': 'external notMain = 8;'})
    doc = metadata(artifact)
    fields = doc['externals']
    assert fields['later'] == {'mutable': False}, fields
    assert 'nested' not in fields and 'notMain' not in fields
    nested = next(f for f in doc['functions'] if f['functionId'] == 2)
    for summary in nested.get('analysis', {}).get('summaries', []):
        assert 'result' not in summary, nested
    for name in ['callback', 'uncertain']:
        record = next(f for f in doc['functions'] if f['functionId'] == fields[name]['functionId'])
        assert 'analysis' not in record, record

    # Decisions 86–90: archives alone must retain dynamic/alias semantics.
    # Compilation is a separate invocation requiring empty stdout, even though
    # every dependency initializer below has an observable output operation.
    p = suite.project('mutable-function-contract')
    library = p.compile('library', '''
        println("mutable init");
        external var calculate = func(x) { return x + 1; };
        calculate(3);
    ''')
    assert exports(library)['calculate'] == {'mutable': True}
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.run('''
        const lib = import "library@1";
        external first = lib.calculate(3);
        lib.calculate = func(x) { return x * 2; };
        external second = lib.calculate(3);
        println(first); println(second);
    ''', 'mutable init\n4\n6\n')
    assert exports(app)['first'] == {'mutable': False}
    assert exports(app)['second'] == {'mutable': False}

    p = suite.project('constant-function-effects')
    library = p.compile('library', '''
        println("counter init");
        var counter = 0;
        external next = func() { counter += 1; return counter; };
        next();
    ''')
    doc = metadata(library)
    record = next(f for f in doc['functions']
                  if f['functionId'] == doc['externals']['next']['functionId'])
    summaries = record['analysis']['summaries']
    assert any(set(s.get('effects', {}).get('may', [])) ==
               {'externalRead', 'externalWrite'} for s in summaries), record
    assert all('value' not in s.get('result', {}) for s in summaries), record
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.run('''
        const lib = import "library@1";
        external first = lib.next(); external second = lib.next();
        println(first); println(second);
    ''', 'counter init\n2\n3\n')
    assert 'value' not in exports(app)['first']
    assert 'value' not in exports(app)['second']

    p = suite.project('callback-contract')
    library = p.compile('library', '''
        println("callback init");
        external apply = func(callback, value) { return callback(value); };
        apply(func(x) { return x; }, 1);
    ''')
    doc = metadata(library)
    record = next(f for f in doc['functions']
                  if f['functionId'] == doc['externals']['apply']['functionId'])
    # Observing a pure callback does not prove purity for arbitrary callbacks.
    assert 'analysis' not in record, record
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.run('''
        const lib = import "library@1";
        var state = 0;
        lib.apply(func(x) { state = x; println(x); return x; }, 5);
        external observed = state;
        println(observed);
        lib.apply(func(x) { state += x; return x; }, 2);
        println(state);
    ''', 'callback init\n5\n5\n7\n')
    assert 'value' not in exports(app)['observed'], exports(app)
    # Callback exceptions must not become a normal result or disappear.
    failure = p.compile('throwing', '''
        const lib = import "library@1";
        lib.apply(func(x) { println("throwing callback"); throw x; }, "failure");
        println("unreachable");
    ''')
    p.invoke('binary', failure, expected='callback init\nthrowing callback\n', fail=True)

    p = suite.project('result-alias-contract')
    library = p.compile('library', '''
        println("alias init");
        external identity = func(value) { return value; };
        identity([1, 2]);
    ''')
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.run('''
        const lib = import "library@1";
        const items = [1, 2];
        const result = lib.identity(items);
        result.push("shared");
        external observed = items[2];
        println(items); println(observed);
        items[0] = "reverse";
        println(result);
    ''', 'alias init\n[1,2,"shared"]\nshared\n["reverse",2,"shared"]\n')
    assert exports(app)['observed'].get('type') != 'integer', exports(app)

    p = suite.project('nested-sharing-contract')
    p.compile('library', '''
        println("copy init");
        external copy = func(value) { return value.clone(); };
        copy([[1]]);
    ''')
    shutil.rmtree(p.root / 'sources' / 'library-1')
    app = p.run('''
        const lib = import "library@1";
        const items = [[1]];
        const result = lib.copy(items);
        result.push([2]);
        result[0].push("shared");
        external observed = items[0][1];
        println(items); println(result); println(observed);
        items[0][0] = "reverse";
        println(result);
    ''', 'copy init\n[[1,"shared"]]\n[[1,"shared"],[2]]\nshared\n'
           '[["reverse","shared"],[2]]\n')
    assert exports(app)['observed'].get('type') != 'integer', exports(app)

    p = suite.project('static-call-diagnostics')
    p.compile('values', 'external scalar = 42; external var dynamic = 42;')
    app = p.compile('app', '''
        const m = import "values@1";
        const alias = m.scalar;
        try { m.scalar(); } catch(e) { println("direct caught"); }
        try { alias(); } catch(e) { println("alias caught"); }
        m.dynamic = func() { return "dynamic"; }; println(m.dynamic());
    ''')
    # Try/catch bodies are conservatively unmodeled by the existing interpreter;
    # they remain runtime checks, not invented static proofs.
    p.invoke('binary', app, fail=True)
    app.unlink()
    p.run('const m = import "values@1"; m.dynamic = func(){return "dynamic";}; '
          'println(m.dynamic());', 'dynamic\n', version='2')
    for name, call in [('direct', 'm.scalar()'), ('alias', 'alias()')]:
        artifact = p.compile(name, 'const m = import "values@1"; '
                             'const alias = m.scalar; ' + call + ';')
        artifact.unlink()
        diagnostic = p.invoke('compile', p.root / ('sources/' + name + '-1/module.gmod'), artifact)
        assert b'statically known value is not callable' in diagnostic.stderr, diagnostic.stderr
    dynamic = p.compile('dynamic', 'const m = import "values@1"; m.dynamic();')
    dynamic.unlink()
    diagnostic = p.invoke('compile', p.root / 'sources/dynamic-1/module.gmod', dynamic)
    assert b'not callable' not in diagnostic.stderr, diagnostic.stderr

    p = suite.project('initialization-preserved')
    p.compile('failure', 'external answer = 42; println("before throw"); throw "broken";')
    app = p.compile('app', 'const m = import "failure@1"; println(m.answer);')
    p.invoke('binary', app, expected='before throw\n', fail=True)

print(f'Module interface checks passed: {suite.count}')
