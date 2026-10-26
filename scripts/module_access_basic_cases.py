# Copyright 2026 Ivan Kniazkov
"""AC-01–AC-10 across actual source-file and compiled-module boundaries."""

import json

from module_execution_support import put


def run(suite):
    def reject(name, code, diagnostic):
        p = suite.project(name)
        directory = p.root / "sources"
        put(directory / "main.goat", 'include "negative.goat";')
        put(directory / "negative.goat", code)
        descriptor = put(directory / "module.gmod", json.dumps({
            "formatVersion": 1, "name": "app", "version": "1", "main": "main.goat"}))
        artifact = p.root / "app.gbin"
        result = p.invoke("compile", descriptor, artifact, fail=True)
        assert diagnostic in result.stderr.decode("utf-8"), result.stderr
        assert not artifact.exists()

    def fixture(number, name, source, consumer, expected):
        # The same producer is compiled as an included sibling and as a dependency.
        # In both cases its entry reexports the implementation's original values.
        for imported in (False, True):
            p = suite.project(f'ac-{number:02d}-{name}-' + ('import' if imported else 'include'))
            wrapper = 'external api = include "implementation.goat";'
            if imported:
                p.compile('library', wrapper, files={'implementation.goat': source})
                prefix = 'const a = (import "library@1").api; '
                files = {}
            else:
                prefix = 'const a = (include "library.goat").api; '
                files = {'library.goat': wrapper, 'implementation.goat': source}
            p.run(prefix + consumer, expected, files=files)

    fixture(1, 'declarations',
            'external f = func() { return 1; }; external const g = func() { return 2; }; '
            'external obj = { private var x = 3, y = 4; protected left = 5, right = 6; '
            'external read = func() { return x + y + left + right; }; };',
            'println(a.f()); println(a.g()); println(a.obj.read()); '
            'try { a.f = func() {}; } catch(e) { println(e); } '
            'try { a.g = func() {}; } catch(e) { println(e); }',
            '1\n2\n18\nPROPERTY_IS_CONSTANT\nPROPERTY_IS_CONSTANT\n')

    # Each invalid form lives in a real included compilation unit, and its valid
    # neighbor is compiled independently: rejection must not be a missing include.
    for index, declaration in enumerate(('public private var x = 1;',
                                          'private obj.x = 2;',
                                          'protected obj["x"] = 3;')):
        reject(f'ac-02-syntax-{index}', 'var obj = {}; ' + declaration,
               'Exactly one access modifier' if index == 0
               else 'Expected name with value assignment')
    fixture(2, 'assignment-keeps-modifier',
            'external obj = { private var x = 1; external change = func() { x = 2; }; '
            'external read = func() { return x; }; };',
            'a.obj.change(); println(a.obj.read()); '
            'try { println(a.obj.x); } catch(e) { println(e); }', '2\nACCESS_DENIED\n')

    fixture(3, 'body-dot-index',
            'external obj = { private var x = 42; println(x); '
            'external read = func() { return this["x"]; }; };',
            'println(a.obj.read()); const readDot = func(value) { return value.x; }; '
            'const readIndex = func(value, key) { return value[key]; }; '
            'try { readDot(a.obj); } catch(e) { println(e); } '
            'try { readIndex(a.obj, "x"); } catch(e) { println(e); }',
            '42\n42\nACCESS_DENIED\nACCESS_DENIED\n')
    # A statically proven denial in an included file must stop compilation even
    # inside try. Dynamic counterparts above must compile and throw in the VM.
    for index, access in enumerate(('obj.x', 'obj["x"]')):
        reject(f'ac-03-static-{index}',
               'const obj = { private var x = 42; }; '
               'try { println(' + access + '); } catch(e) {}',
               'Access denied: read of x (declaration at')

    for method in ('clone', 'flat'):
        fixture(4, method,
                'const outsider = func() { return this.x; }; '
                'external obj = { private var x = 10; external read = outsider; };',
                'const copy = a.obj.' + method + '(); '
                'try { copy.read(); } catch(e) { println(e); }', 'ACCESS_DENIED\n')

    fixture(5, 'ordinary-method-move',
            'external obj = { private var x = 10; '
            'external read = func(target) { return target.x; }; };',
            'var b = { private var x = 20; }; b.read = a.obj.read; '
            'println(a.obj.read(a.obj)); '
            'try { b.read(b); } catch(e) { println(e); }', '10\nACCESS_DENIED\n')

    for visibility in ('private', 'protected'):
        fixture(6, visibility,
                'external make = func(value) { return { ' + visibility + ' var x = value; '
                'external sumWith = func(other) { return x + other.x; }; }; };',
                'const left = a.make(10); const right = a.make(20); '
                'println(left.sumWith(left)); '
                'try { left.sumWith(right); } catch(e) { println(e); }', '20\nACCESS_DENIED\n')

    fixture(7, 'escaped-closure',
            'external obj = { private var x = 42; external reader = func() { '
            'return func() { return x; }; }; };',
            'const read = a.obj.reader(); println(read()); '
            'try { println(a.obj.x); } catch(e) { println(e); } println(read());',
            '42\nACCESS_DENIED\n42\n')

    fixture(8, 'nested-capture',
            'external outer = { private var secret = 42; external inner = { '
            'external read = func() { return secret; }; }; };',
            'println(a.outer.inner.read()); '
            'try { println(a.outer.secret); } catch(e) { println(e); }', '42\nACCESS_DENIED\n')

    fixture(9, 'descendant-rights',
            'external Base = { protected var x = 10; private var secret = 7; '
            'external readSecret = func() { return secret; }; };',
            'var child = a.Base -> { public fromBody = x; '
            'public readBase = func() { return a.Base.x; }; '
            'public forbidden = func() { return a.Base.secret; }; }; '
            'println(child.fromBody); println(child.readBase()); println(child.readSecret()); '
            'try { child.forbidden(); } catch(e) { println(e); } '
            'try { println(a.Base.x); } catch(e) { println(e); }',
            '10\n10\n7\nACCESS_DENIED\nACCESS_DENIED\n')

    for method in ('clone', 'flat'):
        fixture(10, 'hidden-shadow-' + method,
                'const Base = { external var x = 1; }; '
                'external obj = Base -> { private var x = 2; '
                'external read = func() { return x; }; };',
                'println(a.obj.read()); const copy = a.obj.' + method + '(); '
                'println(copy.read()); const read = func(value) { return value.x; }; '
                'try { read(a.obj); } catch(e) { println(e); } '
                'try { read(copy); } catch(e) { println(e); }',
                '2\n2\nACCESS_DENIED\nACCESS_DENIED\n')
