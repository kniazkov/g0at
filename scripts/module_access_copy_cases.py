# Copyright 2026 Ivan Kniazkov
"""AC-11–AC-22: copy access semantics across real file and module boundaries."""


def exercise(suite, case, definitions, body, expected):
    """Run identical observations through include and a compiled reexport chain."""
    for boundary in ('include', 'import'):
        p = suite.project(f'{case}-{boundary}')
        if boundary == 'include':
            p.run('const api = (include "facade.goat").api; ' + body, expected, files={
                'facade.goat': 'external api = include "definitions.goat";',
                'definitions.goat': definitions,
            })
        else:
            p.compile('library', 'external api = include "definitions.goat";',
                      files={'definitions.goat': definitions})
            p.compile('facade', 'external api = (import "library@1").api;')
            p.run('const api = (import "facade@1").api; ' + body, expected)


def run(suite):
    exercise(suite, 'AC-11',
             'external obj = { private var x = 42; '
             'external getX = func() { return this.x; }; };',
             'const copy = api.obj.flat(); println(copy.getX()); '
             'try { println(copy.x); } catch(e) { println(e); } '
             'try { println(copy["x"]); } catch(e) { println(e); } '
             'for (key in copy) println(key);',
             '42\nACCESS_DENIED\nACCESS_DENIED\ngetX\n')

    exercise(suite, 'AC-12',
             'const A = { private var x = 10; external getA = func() { return this.x; }; }; '
             'const B = A -> { private var x = 20; external getB = func() { return this.x; }; }; '
             'external obj = B -> { external sum = func() { return this.getA() + this.getB(); }; };',
             'const copy = api.obj.flat(); println(copy.getA()); println(copy.getB()); '
             'println(copy.sum()); try { println(copy.x); } catch(e) { println(e); }',
             '10\n20\n30\nACCESS_DENIED\n')

    exercise(suite, 'AC-13',
             'const A = { protected var x = 10; external captured = func() { return x; }; '
             'external current = func() { return this.x; }; }; '
             'external obj = A -> { protected var x = 20; };',
             'println(api.obj.captured()); println(api.obj.current()); '
             'const copy = api.obj.flat(); println(copy.captured()); println(copy.current()); '
             'try { println(copy.x); } catch(e) { println(e); }',
             '10\n20\n10\n20\nACCESS_DENIED\n')

    for operation in ('clone', 'flat'):
        exercise(suite, f'AC-14-{operation}',
                 'external obj = { private var x = 10; external getX = func() { return x; }; '
                 'external setX = func(value) { x = value; }; '
                 'external viaThis = func() { return this.x; }; };',
                 f'const copy = api.obj.{operation}(); copy.setX(20); '
                 'println(api.obj.getX()); println(copy.getX()); println(copy.viaThis()); '
                 'api.obj.setX(30); println(copy.getX()); println(api.obj.viaThis());',
                 '10\n20\n20\n20\n30\n')

        exercise(suite, f'AC-15-{operation}',
                 'var counter = 0; external count = func() { return counter; }; '
                 'external obj = { private var x = 10; external next = func() { '
                 'counter += 1; x += 1; return x; }; };',
                 f'const copy = api.obj.{operation}(); println(copy.next()); '
                 'println(api.obj.next()); println(api.count()); '
                 'try { println(api.counter); } catch(e) { println(e); }',
                 '11\n11\n2\nACCESS_DENIED\n')

        exercise(suite, f'AC-16-{operation}',
                 'external obj = { private var x = 10; external setX = func(value) { x = value; }; '
                 'external inner = { external getX = func() { return x; }; }; };',
                 f'const copy = api.obj.{operation}(); copy.setX(20); '
                 'println(copy.inner == api.obj.inner); println(copy.inner.getX()); '
                 'api.obj.setX(30); println(copy.inner.getX());', 'true\n10\n30\n')

        for modifier in ('private', 'protected'):
            exercise(suite, f'AC-17-{operation}-{modifier}',
                     f'external A = {{ {modifier} var x = 10; '
                     'external setLocal = func(value) { x = value; }; '
                     'external local = func() { return x; }; '
                     'external original = func() { return A.x; }; };',
                     f'const copy = api.A.{operation}(); copy.setLocal(20); '
                     'println(copy.local()); println(copy.original()); '
                     'println(api.A.local()); try { println(copy.x); } catch(e) { println(e); }',
                     '20\n10\n10\nACCESS_DENIED\n')

        exercise(suite, f'AC-18-{operation}',
                 'external obj = { private var x = 10; '
                 'external readOther = func(other) { return other.x; }; };',
                 f'const copy = api.obj.{operation}(); '
                 'println(copy.readOther(copy)); println(copy.readOther(api.obj)); '
                 'try { println(api.obj.readOther(copy)); } catch(e) { println(e); } '
                 'println(api.obj.readOther(api.obj));', '10\n10\nACCESS_DENIED\n10\n')

        exercise(suite, f'AC-19-{operation}',
                 'external Base = { private var x = 0; '
                 'external next = func() { x += 1; return x; }; }; '
                 'external obj = Base -> {};',
                 f'const copy = api.obj.{operation}(); '
                 'println(api.obj.next()); println(copy.next()); '
                 'println(copy.next == api.obj.next); println(copy.instanceOf(api.Base));',
                 '1\n2\ntrue\ntrue\n' if operation == 'clone' else '1\n1\nfalse\nfalse\n')

        exercise(suite, f'AC-20-{operation}',
                 'external obj = { private var x = 10; external f = func() { return x; }; '
                 'external g = f; external h = func() { return x; }; };',
                 f'const copy = api.obj.{operation}(); println(copy.f == copy.g); '
                 'println(copy.f == api.obj.f); println(copy.g()); '
                 'println(copy.f == copy.h); println(copy.h()); println(api.obj.f == api.obj.g);',
                 'true\nfalse\n10\nfalse\n10\ntrue\n')

    exercise(suite, 'AC-21',
             'external A = { private var x = 0; external inc = func() { x += 1; return x; }; '
             'external read = func() { return x; }; }; '
             'const Left = A -> { external viaLeft = func() { return this.inc(); }; }; '
             'const Right = A -> { external viaRight = func() { return this.read(); }; }; '
             'external obj = [Left, Right] -> {};',
             'const copy = api.obj.flat(); println(copy.viaLeft()); println(copy.viaRight()); '
             'println(api.A.read()); println(copy.viaLeft()); println(copy.viaRight()); '
             'println(api.A.read());', '1\n1\n0\n2\n2\n0\n')

    exercise(suite, 'AC-22',
             'external Left = { external var x = 1; }; '
             'external Right = { external var x = 2; }; '
             'external obj = [Left, Right] -> {};',
             'const copy = api.obj.flat(); println(copy.x == api.obj.x); '
             'println(copy.instanceOf(Object)); println(copy.instanceOf(api.Left)); '
             'println(copy.instanceOf(api.Right)); copy.x = 9; '
             'println(api.Left.x); println(api.Right.x);', 'true\ntrue\nfalse\nfalse\n1\n2\n')
