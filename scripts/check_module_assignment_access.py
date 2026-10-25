#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Assignment access and exact evaluation-order scenarios 13–25 (#227)."""
import sys
import subprocess
from pathlib import Path
import tempfile

from module_execution_support import ExecutionSuite


with tempfile.TemporaryDirectory(prefix='goat-assignment-access-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)

    # 13. Dynamic indexing and member access enforce identical field rights.
    for access in ['target.shared', 'target[key]']:
        p = suite.project('13-shared-' + str(suite.count))
        p.run('const a = include "a.goat"; const change = func(target, key) { '
              'println(' + access + '); ' + access + ' = 21; println(' + access + '); }; '
              'change(a.obj, "shared"); println(a.read());', '20\n21\n21\n', files={
                  'a.goat': 'public obj = { var hidden = 10; public var shared = 20; }; '
                            'public read = func() { return obj.shared; };'})
    for access in ['target.hidden', 'target[key]']:
        p = suite.project('13-hidden-' + str(suite.count))
        p.run('const a = include "a.goat"; var effects = 0; '
              'const rhs = func() { effects += 1; return 99; }; '
              'const read = func(target, key) { return ' + access + '; }; '
              'const write = func(target, key) { ' + access + ' = rhs(); }; '
              'try { read(a.obj, "hidden"); } catch(e) { println(e); } '
              'try { write(a.obj, "hidden"); } catch(e) { println(e); } '
              'println(effects); println(a.read());',
              'ACCESS_DENIED\nACCESS_DENIED\n0\n10\n', files={
                  'a.goat': 'public obj = { var hidden = 10; }; '
                            'public read = func() { return obj.hidden; };'})

    # 14–16. Evaluate object/key once, then reject before any RHS side effect.
    for rule, operation, field, constant in [(14, '+=', 'hidden', False),
                                            (15, '=', 'hidden', False),
                                            (15, '=', 'limit', True),
                                            (16, '+=', 'limit', True)]:
        for indexed in [False, True]:
            p = suite.project(f'{rule}-early-{suite.count}')
            lhs = 'target()[key()]' if indexed else 'target().' + field
            declaration = 'public limit = 10;' if constant else 'var hidden = 10;'
            p.run('const a = include "a.goat"; var events = ""; '
                  'var objects = 0; var keys = 0; var rights = 0; '
                  'const target = func() { objects += 1; events += "o"; return a.obj; }; '
                  'const key = func() { keys += 1; events += "k"; return "' + field + '"; }; '
                  'const rhs = func() { rights += 1; events += "r"; return 2; }; '
                  'try { ' + lhs + ' ' + operation + ' rhs(); } catch(e) { println(e); } '
                  'println(events); println(objects); println(keys); println(rights); println(a.read());',
                  ('PROPERTY_IS_CONSTANT\n' if constant else 'ACCESS_DENIED\n') +
                  ('ok\n1\n1\n0\n10\n' if indexed else 'o\n1\n0\n0\n10\n'), files={
                      'a.goat': 'public obj = { ' + declaration + ' }; '
                                'public read = func() { return obj.' + field + '; };'})

    # 15. A foreign object's absent field is not extensible, even if its value is exported.
    for indexed in [False, True]:
        p = suite.project('15-foreign-extension-' + str(suite.count))
        p.compile('library', 'external obj = {}; external read = func() { return obj; };')
        lhs = 'target()[key()]' if indexed else 'target().newField'
        p.run('const lib = import "library@1"; var events = ""; '
              'const target = func() { events += "o"; return lib.obj; }; '
              'const key = func() { events += "k"; return "newField"; }; '
              'const rhs = func() { events += "r"; return 1; }; '
              'try { ' + lhs + ' = rhs(); } catch(e) { println(e); } '
              'println(events); println(lib.read());',
              'FOREIGN_OBJECT\n' + ('ok\n' if indexed else 'o\n') + '{ }\n')

    # Imported objects retain field visibility and constness for both lvalue forms.
    for operation in ['=', '+=']:
        for indexed in [False, True]:
            p = suite.project('foreign-fields-' + str(suite.count))
            p.compile('library', 'external obj = { public var hidden = 10; '
                      'external limit = 20; external var shared = 30; }; '
                      'external read = func() { println(obj.hidden); println(obj.limit); println(obj.shared); };')
            lhs = 'target()[key()]' if indexed else 'target().hidden'
            const_lhs = 'target()[key()]' if indexed else 'target().limit'
            shared_lhs = 'target()[key()]' if indexed else 'target().shared'
            p.run('const lib = import "library@1"; var events = ""; var name = "hidden"; '
                  'const target = func() { events += "o"; return lib.obj; }; '
                  'const key = func() { events += "k"; return name; }; '
                  'const rhs = func() { events += "r"; return 2; }; '
                  'try { ' + lhs + ' ' + operation + ' rhs(); } catch(e) { println(e); } '
                  'println(events); events = ""; name = "limit"; '
                  'try { ' + const_lhs + ' ' + operation + ' rhs(); } catch(e) { println(e); } '
                  'println(events); events = ""; name = "shared"; '
                  + shared_lhs + ' ' + operation + ' rhs(); println(events); lib.read();',
                  'ACCESS_DENIED\n' + ('ok\n' if indexed else 'o\n') +
                  'PROPERTY_IS_CONSTANT\n' + ('ok\nokr\n' if indexed else 'o\nor\n') +
                  '10\n20\n' + ('2\n' if operation == '=' else '32\n'))

    # 17–18. Compound assignment reads once; throwing RHS retains its own writes only.
    for indexed in [False, True]:
        lhs = 'target()[key()]' if indexed else 'target().x'
        for throwing in [False, True]:
            p = suite.project(('18' if throwing else '17') + '-value-' + str(suite.count))
            p.run('var obj = { public var x = 10; }; var events = ""; '
                  'var objects = 0; var keys = 0; var rights = 0; '
                  'const target = func() { objects += 1; events += "o"; return obj; }; '
                  'const key = func() { keys += 1; events += "k"; return "x"; }; '
                  'const rhs = func() { rights += 1; events += "r"; obj.x = 100; ' +
                  ('throw "rhs failed";' if throwing else 'return 2;') + ' }; '
                  'try { ' + lhs + ' += rhs(); } catch(e) { println(e); } '
                  'println(obj.x); println(events); println(objects); println(keys); println(rights);',
                  ('rhs failed\n100\n' if throwing else '12\n') +
                  ('okr\n1\n1\n1\n' if indexed else 'or\n1\n0\n1\n'))

    # 19–20. Destination object and key are captured before RHS; variable rebinding is irrelevant.
    for operation in ['=', '+=']:
        for indexed in [False, True]:
            p = suite.project('19-20-captured-' + str(suite.count))
            lhs = 'target()[index()]' if indexed else 'target().x'
            p.run('var obj = { public var x = 10; public var y = 20; }; '
                  'const original = obj; var key = "x"; var events = ""; '
                  'const target = func() { events += "o"; return obj; }; '
                  'const index = func() { events += "k"; return key; }; '
                  'const rhs = func() { events += "r"; key = "y"; '
                  'obj = { public var x = 100; public var y = 200; }; return 2; }; '
                  + lhs + ' ' + operation + ' rhs(); '
                  'println(original.x); println(original.y); println(obj.x); println(obj.y); '
                  'println(key); println(events);',
                  ('2\n' if operation == '=' else '12\n') + '20\n100\n200\ny\n' +
                  ('okr\n' if indexed else 'or\n'))

    # Object/key exceptions stop evaluation immediately, preserving prior effects.
    for operation in ['=', '+=']:
        for failing in ['object', 'key']:
            p = suite.project('left-throws-' + str(suite.count))
            p.run('var obj = { public var x = 10; }; var events = ""; '
                  'const target = func() { events += "o"; ' +
                  ('throw "object failed";' if failing == 'object' else 'return obj;') + ' }; '
                  'const key = func() { events += "k"; throw "key failed"; }; '
                  'const rhs = func() { events += "r"; return 2; }; '
                  'try { target()[key()] ' + operation + ' rhs(); } catch(e) { println(e); } '
                  'println(events); println(obj.x);',
                  ('object failed\no\n' if failing == 'object' else 'key failed\nok\n') + '10\n')

    # A key may rebind the object variable and mutate the original before its value is read.
    for operation in ['=', '+=']:
        p = suite.project('key-mutates-target-' + str(suite.count))
        p.run('var obj = { public var x = 10; public var y = 20; }; const original = obj; '
              'var events = ""; var name = "y"; '
              'const target = func() { events += "o"; name = "x"; return obj; }; '
              'const key = func() { events += "k"; original.x = 30; '
              'obj = { public var x = 100; }; return name; }; '
              'const rhs = func() { events += "r"; original.x = 50; return 2; }; '
              'target()[key()] ' + operation + ' rhs(); '
              'println(original.x); println(original.y); println(obj.x); println(name); println(events);',
              ('2\n' if operation == '=' else '32\n') + '20\n100\nx\nokr\n')

    # 21. RHS may create an inaccessible field: final write must recheck, preserving RHS effects.
    for indexed in [False, True]:
        p = suite.project('21-final-private-' + str(suite.count))
        lhs = 'target()[key()]' if indexed else 'target().x'
        p.run('const a = include "a.goat"; var events = ""; '
              'const target = func() { events += "o"; return a.obj; }; '
              'const key = func() { events += "k"; return "x"; }; '
              'const rhs = func() { events += "r"; return a.prepare(); }; '
              'try { ' + lhs + ' = rhs(); } catch(e) { println(e); } '
              'println(events); println(a.read()); println(a.calls());',
              'ACCESS_DENIED\n' + ('okr\n' if indexed else 'or\n') + '100\n1\n', files={
                  'a.goat': 'public obj = {}; var effects = 0; '
                            'public prepare = func() { effects += 1; obj.x = 100; return 2; }; '
                            'public read = func() { return obj.x; }; '
                            'public calls = func() { return effects; };'})

    # 22. Early write validation does not insert a placeholder visible to RHS.
    for indexed in [False, True]:
        p = suite.project('22-no-placeholder-' + str(suite.count))
        lhs = 'target()[key()]' if indexed else 'target().x'
        p.run('var obj = {}; var events = ""; '
              'const target = func() { events += "o"; return obj; }; '
              'const key = func() { events += "k"; return "x"; }; '
              'const rhs = func() { events += "r"; println(obj); throw "stop"; }; '
              'try { ' + lhs + ' = rhs(); } catch(e) { println(e); } '
              'println(obj); println(events);', '{ }\nstop\n{ }\n' + ('okr\n' if indexed else 'or\n'))

    # 23. An accessible field created by RHS is updated, not replaced with a new provenance.
    for indexed in [False, True]:
        p = suite.project('23-update-created-' + str(suite.count))
        lhs = 'target()[key()]' if indexed else 'target().x'
        p.run('public obj = {}; var events = ""; '
              'const target = func() { events += "o"; return obj; }; '
              'const key = func() { events += "k"; return "x"; }; '
              'const rhs = func() { events += "r"; obj.x = 100; return 2; }; '
              + lhs + ' = rhs(); println(obj.x); println(events); '
              'const observer = include "observer.goat"; observer.check(obj);',
              '2\n' + ('okr\n' if indexed else 'or\n') + 'ACCESS_DENIED\n', files={
                  'observer.goat': 'public check = func(value) { '
                                   'try { println(value.x); } catch(e) { println(e); } };'})

    # 24–25. An array destination is a saved position, validated again after mutation.
    for operation in ['=', '+=']:
        for clearing in [False, True]:
            p = suite.project(('25' if clearing else '24') + '-array-' + str(suite.count))
            p.run('var items = [10, 20, 30]; var events = ""; var saved = items; var index = 1; '
                  'const target = func() { events += "o"; return items; }; '
                  'const key = func() { events += "k"; return index; }; '
                  'const rhs = func() { events += "r"; ' +
                  ('items.clear();' if clearing else 'items.remove(0);') +
                  ' index = 0; items = [99]; return 2; }; '
                  'try { target()[key()] ' + operation + ' rhs(); } catch(e) { println(e); } '
                  'println(saved); println(items); println(index); println(events);',
                  ('INVALID_ARGUMENT\n[ ]\n' if clearing else
                   ('[20,2]\n' if operation == '=' else '[20,22]\n')) + '[99]\n0\nokr\n')

    # Module packaging deliberately uses no optimization. Exercise the optimizer's
    # lvalue handling through the CLI too, when its executable is supplied.
    if len(sys.argv) > 2:
        goat = str(Path(sys.argv[2]).resolve())
        cli_cases = []
        for operation in ['=', '+=']:
            cli_cases.append(('constant-' + operation,
                'const x = 10; var effects = 0; '
                'const rhs = func() { effects += 1; return 2; }; '
                'try { x ' + operation + ' rhs(); } catch(e) { println(e); } '
                'println(effects); println(x);', 'PROPERTY_IS_CONSTANT\n0\n10\n'))
            cli_cases.append(('capture-' + operation,
                'var obj = { public var x = 10; public var y = 20; }; const original = obj; '
                'var key = "x"; var effects = ""; '
                'const rhs = func() { effects += "r"; key = "y"; '
                'obj = { public var x = 100; public var y = 200; }; return 2; }; '
                'println(obj[key] ' + operation + ' rhs()); '
                'println(original.x); println(original.y); println(obj.x); println(obj.y); println(effects);',
                ('2\n2\n' if operation == '=' else '12\n12\n') + '20\n100\n200\nr\n'))
            cli_cases.append(('key-mutation-' + operation,
                'var obj = { public var x = 10; public var y = 20; }; const original = obj; '
                'var name = "y"; var events = ""; '
                'const target = func() { events += "o"; name = "x"; return obj; }; '
                'const key = func() { events += "k"; original.x = 30; '
                'obj = { public var x = 100; }; return name; }; '
                'const rhs = func() { events += "r"; original.x = 50; return 2; }; '
                'println(target()[key()] ' + operation + ' rhs()); '
                'println(original.x); println(obj.x); println(events);',
                ('2\n2\n' if operation == '=' else '32\n32\n') + '100\nokr\n'))
            cli_cases.append(('rhs-throws-' + operation,
                'var obj = { public var x = 10; }; var events = ""; '
                'const target = func() { events += "o"; return obj; }; '
                'const key = func() { events += "k"; return "x"; }; '
                'const rhs = func() { events += "r"; obj.x = 100; throw "stop"; }; '
                'try { target()[key()] ' + operation + ' rhs(); } catch(e) { println(e); } '
                'println(obj.x); println(events);', 'stop\n100\nokr\n'))
            for clearing in [False, True]:
                cli_cases.append(('array-' + operation + '-' + str(clearing),
                    'var items = [10, 20, 30]; const original = items; var key = 1; '
                    'const rhs = func() { ' + ('items.clear();' if clearing else 'items.remove(0);') +
                    'items = [99]; key = 0; return 2; }; '
                    'try { items[key] ' + operation + ' rhs(); } catch(e) { println(e); } '
                    'println(original); println(items);',
                    ('INVALID_ARGUMENT\n[ ]\n' if clearing else
                     '[20,2]\n' if operation == '=' else '[20,22]\n') + '[99]\n'))
        cli_cases.append(('chain',
            'var obj = { public var x = 10; public var y = 20; }; var events = ""; '
            'const target = func() { events += "o"; return obj; }; '
            'const key = func() { events += "k"; return "x"; }; '
            'const rhs = func() { events += "r"; return 2; }; '
            'var value = 0; println(value = target()[key()] = obj.y += rhs()); '
            'println(value); println(obj.x); println(obj.y); println(events);',
            '22\n22\n22\n22\nokr\n'))
        for optimization in ['none', 'all']:
            for name, code, expected in cli_cases:
                source = Path(temporary) / 'cli.goat'
                source.write_text(code, encoding='utf-8')
                command = [goat, '--optimize', optimization, str(source)]
                result = subprocess.run(command, capture_output=True, timeout=60, cwd=suite.unrelated)
                suite.count += 1
                actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
                assert result.returncode == 0 and actual == expected, (
                    name, optimization, command, result.returncode, actual, expected, result.stderr)

print(f'Module assignment access checks passed: {suite.count}')
