#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Multi-file/module execution of function and field provenance rules 1–12 (#228)."""
import sys
import tempfile

from module_execution_support import ExecutionSuite


with tempfile.TemporaryDirectory(prefix='goat-function-provenance-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)

    # 1. Observation uses the executing function's file, not its caller's file.
    p = suite.project('01-declaring-file')
    p.run('const a = include "a.goat"; println(a.obj); a.show(a.obj); '
          'try { println(a.obj.hidden); } catch(e) { println("denied"); println(e); }',
          '{"shared":20}\n{"hidden":10,"shared":20}\ndenied\nACCESS_DENIED\n', files={
              'a.goat': 'public obj = { var hidden = 10; public var shared = 20; }; '
                        'public show = func(value) { println(value); };'})

    # 2. A callback retains its own rights, including across exception unwinding.
    p = suite.project('02-callback')
    p.run('const a = include "a.goat"; a.inspect(func(value) { println(value); }); '
          'try { a.inspect(func(value) { return value.hidden; }); } '
          'catch(e) { println("callback denied"); println(e); } '
          'println(a.read()); try { println(a.obj.hidden); } '
          'catch(e) { println("caller still denied"); println(e); }',
          '{"shared":20}\ncallback denied\nACCESS_DENIED\n10\ncaller still denied\nACCESS_DENIED\n', files={
              'a.goat': 'public obj = { var hidden = 10; public var shared = 20; }; '
                        'public inspect = func(callback) { callback(obj); }; '
                        'public read = func() { return obj.hidden; };'})

    # 3. Escaped closures keep both captures and declaring-file rights.
    p = suite.project('03-closure')
    p.run('const a = include "a.goat"; const read = a.makeReader(10); '
          'const other = a.makeReader(20); println(read()); println(other()); '
          'println(read()); const holder = { public f = read; }; println(holder.f());',
          '11\n21\n12\n13\n', files={
              'a.goat': 'public makeReader = func(start) { '
                        'const obj = { var hidden = start; }; '
                        'return func() { obj.hidden += 1; return obj.hidden; }; };'})

    # 4. Storing a foreign function in an object's method field grants no rights.
    p = suite.project('04-method-storage')
    p.run('const a = include "a.goat"; const b = include "b.goat"; '
          'println(a.obj.read == b.readHidden); '
          'try { a.obj.read(a.obj); } catch(e) { println("method denied"); println(e); } '
          'try { a.call(); } catch(e) { println("owner call denied"); println(e); } '
          'println(a.read());', 'true\nmethod denied\nACCESS_DENIED\nowner call denied\nACCESS_DENIED\n10\n', files={
              'a.goat': 'const b = include "b.goat"; '
                        'public obj = { var hidden = 10; public read = b.readHidden; }; '
                        'public call = func() { return obj.read(obj); }; '
                        'public read = func() { return obj.hidden; };',
              'b.goat': 'public readHidden = func(obj) { return obj.hidden; };'})

    # 5. Reexport chains preserve the exact function object and original context.
    p = suite.project('05-function-reexport')
    p.compile('library', 'const i = include "internal.goat"; external read = i.read; '
              'external makeReader = i.makeReader; external obj = i.obj;', files={
                  'internal.goat': 'public obj = { var hidden = 10; }; '
                                   'public read = func() { return obj.hidden; }; '
                                   'public makeReader = func(n) { return func() { '
                                   'return obj.hidden + n; }; };'})
    p.compile('facade', 'const lib = import "library@1"; external read = lib.read; '
              'external makeReader = lib.makeReader; external obj = lib.obj;')
    p.compile('outer', 'const f = import "facade@1"; external read = f.read; '
              'external makeReader = f.makeReader; external obj = f.obj;')
    p.run('const lib = import "library@1"; const f = import "facade@1"; '
          'const o = import "outer@1"; println(lib.read == f.read); '
          'println(f.read == o.read); println(lib.obj == o.obj); println(o.read()); '
          'println(o.makeReader(7)()); try { println(o.obj.hidden); } '
          'catch(e) { println("hidden denied"); println(e); }',
          'true\ntrue\ntrue\n10\n17\nhidden denied\nACCESS_DENIED\n')

    # 6. Export is not recursive: each level checks its own field declaration.
    p = suite.project('06-nested-export')
    p.compile('library', 'external obj = { public var internal = 10; '
              'external var shared = 20; external nested = { public secret = 30; '
              'external value = 40; }; }; '
              'external read = func() { return obj.internal + obj.nested.secret; }; '
              'external getNested = func() { return [obj.nested]; };')
    p.run('const lib = import "library@1"; println(lib.obj); '
          'println(lib.obj.shared); println(lib.obj.nested.value); println(lib.read()); '
          'try { println(lib.obj.internal); } catch(e) { println("internal denied"); println(e); } '
          'try { println(lib.obj.nested.secret); } catch(e) { println("nested denied"); println(e); } '
          'try { lib.obj.internal = 50; } catch(e) { println("write denied"); println(e); } '
          'println(lib.read()); const nested = lib.getNested(); println(nested); '
          'println(nested[0] == lib.obj.nested); '
          'try { println(nested[0]["secret"]); } '
          'catch(e) { println("array nested denied"); println(e); }',
          '{"nested":{"value":40},"shared":20}\n20\n40\n40\ninternal denied\nACCESS_DENIED\nnested denied\nACCESS_DENIED\nwrite denied\nACCESS_DENIED\n40\n[{"value":40}]\ntrue\narray nested denied\nACCESS_DENIED\n')

    # 7. Returning the same object does not transfer the getter's capabilities.
    p = suite.project('07-return-object')
    p.compile('library', 'const obj = { var hidden = 10; external var shared = 20; }; '
              'external getObject = func() { return obj; }; '
              'external read = func() { return obj.hidden; };')
    p.run('const lib = import "library@1"; const obj = lib.getObject(); '
          'println(obj == lib.getObject()); println(obj.shared); println(lib.read()); '
          'try { println(obj.hidden); } catch(e) { println("read denied"); println(e); } '
          'try { obj.hidden = 99; } catch(e) { println("write denied"); println(e); } '
          'println(lib.read()); obj.shared = 25; println(lib.getObject().shared);',
          'true\n20\n10\nread denied\nACCESS_DENIED\nwrite denied\nACCESS_DENIED\n10\n25\n')

    # 8. A constant exported cell is distinct from its mutable array value.
    p = suite.project('08-constant-reference')
    p.compile('library', 'external items = [1, 2]; external get = func() { return items; };')
    p.run('const lib = import "library@1"; const items = lib.items; items.push(3); '
          'println(lib.items); println(items == lib.get()); '
          'try { lib.items = [4, 5]; } catch(e) { println("constant denied"); println(e); } '
          'println(lib.items); println(items == lib.items);',
          '[1,2,3]\ntrue\nconstant denied\nPROPERTY_IS_CONSTANT\n[1,2,3]\ntrue\n')

    # 9. Writes change a value, not its file/module, visibility or constness.
    p = suite.project('09-field-provenance')
    p.compile('library', 'const a = include "a.goat"; include "b.goat"; '
              'external obj = a.obj; external read = a.read; '
              'external siblingRead = (include "c.goat").read;', files={
                  'a.goat': 'public obj = { public var value = 1; external var shared = 3; }; '
                            'public read = func() { return obj.value; };',
                  'b.goat': 'const a = include "a.goat"; a.obj.value = 2; a.obj.shared = 4;',
                  'c.goat': 'const a = include "a.goat"; '
                            'public read = func() { return a.obj.value; };'})
    p.run('const lib = import "library@1"; println(lib.read()); println(lib.siblingRead()); '
          'println(lib.obj.shared); lib.obj.shared = 5; println(lib.obj.shared); '
          'try { println(lib.obj.value); } catch(e) { println("public read denied"); println(e); } '
          'try { lib.obj.value = 9; } catch(e) { println("public write denied"); println(e); } '
          'println(lib.read());', '2\n2\n4\n5\npublic read denied\nACCESS_DENIED\npublic write denied\nACCESS_DENIED\n2\n')

    # 10. A replacement function does not inherit the field's/original function's file.
    p = suite.project('10-replacement-function')
    p.compile('library', 'const a = include "a.goat"; external obj = a.obj; '
              'external call = a.call; external original = a.obj.read; '
              'external replace = (include "b.goat").replace;', files={
                  'a.goat': 'public obj = { var hidden = 10; '
                            'public var read = func() { return hidden; }; }; '
                            'public call = func() { return obj.read(); };',
                  'b.goat': 'const a = include "a.goat"; public replace = func() { '
                            'a.obj.read = func() { return a.obj.hidden; }; };'})
    p.run('const lib = import "library@1"; println(lib.call()); lib.replace(); '
          'try { lib.call(); } catch(e) { println("replacement denied"); println(e); } '
          'println(lib.original()); try { println(lib.obj.read); } '
          'catch(e) { println("field still public"); println(e); }',
          '10\nreplacement denied\nACCESS_DENIED\n10\nfield still public\nACCESS_DENIED\n')

    # 11. Reexport declarations copy values into separate cells, not live bindings.
    p = suite.project('11-independent-cells')
    p.compile('library', 'const i = include "internal.goat"; '
              'external var counter = i.counter; external var items = i.items; '
              'external var obj = i.obj; external internal = i; '
              'external change = func() { i.counter = 8; i.items = [8]; '
              'i.obj = { external var value = 8; }; };', files={
                  'internal.goat': 'external var counter = 0; external var items = [1]; '
                                   'external var obj = { external var value = 1; };'})
    p.run('const lib = import "library@1"; lib.counter = 5; '
          'println(lib.internal.counter); println(lib.items == lib.internal.items); '
          'println(lib.obj == lib.internal.obj); lib.items.push(2); lib.obj.value = 2; '
          'println(lib.internal.items); println(lib.internal.obj.value); '
          'lib.items = [3]; lib.obj = { external var value = 3; }; '
          'println(lib.internal.items); println(lib.internal.obj.value); '
          'lib.change(); println(lib.counter); println(lib.items); println(lib.obj.value); '
          'println(lib.internal.counter); println(lib.internal.items); '
          'println(lib.internal.obj.value);',
          '0\ntrue\ntrue\n[1,2]\n2\n[1,2]\n2\n5\n[3]\n3\n8\n[8]\n8\n')

    # 12. Reexporting a file scope is explicit, but its public fields stay internal.
    p = suite.project('12-file-scope')
    p.compile('library', 'external tools = include "internal.goat"; '
              'external again = include "internal.goat"; '
              'external callHelper = func() { return tools.helper(); };', files={
                  'internal.goat': 'const hidden = 7; public helper = func() { return 10; }; '
                                   'external api = func() { return hidden + 13; };'})
    p.run('const lib = import "library@1"; println(lib.tools == lib.again); '
          'println(lib.tools.api()); println(lib.callHelper()); '
          'try { lib.tools.helper(); } catch(e) { println("helper denied"); println(e); } '
          'try { println(lib.tools.hidden); } catch(e) { println("hidden denied"); println(e); }',
          'true\n20\n10\nhelper denied\nACCESS_DENIED\nhidden denied\nACCESS_DENIED\n')
    invalid = suite.project('12-no-direct-internal-import')
    invalid.compile('library', 'external tools = include "internal.goat";', files={
        'internal.goat': 'external api = func() { return 20; };'})
    invalid.compile('app', 'import "library@1/internal.goat";', fail=True)

print(f'Module function provenance checks passed: {suite.count}')
