#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Production VM per-call module specialization checks (decisions 91–100)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from module_execution_support import put


def spec(types, action='sum', result='integer', fid=1, sid=1, conditions=None):
    guard = {'argumentTypes': types}
    if conditions is not None:
        guard['conditions'] = conditions
    return {'specializationId': sid, 'functionId': fid,
            'symbol': 'goat_fn_%d_v1' % sid,
            'signature': {'id': 'goat.adapter.v1', 'arguments': types, 'result': result},
            'when': guard}, action


def condition(index, operation, value, kind='integer'):
    return {'argument': index, 'operation': operation,
            'constant': {'type': kind, 'value': value}}


def main():
    executable = str(Path(sys.argv[1]).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='goat-module-dispatch-') as temporary:
        root = Path(temporary)

        def run(name, source, expected, specifications, calls, mode='auto', fail=False,
                files=None, resolver_failure=False, diagnostic=None, binding_failure=False, resolutions=None):
            nonlocal count
            directory = root / name / mode
            path = put(directory / 'main.goat', source)
            for filename, contents in (files or {}).items():
                put(directory / filename, contents)
            configuration = {'specializations': [s[0] for s in specifications],
                             'actions': [s[1] for s in specifications],
                             'resolverFailure': resolver_failure, 'bindingFailure': binding_failure}
            config = put(directory / 'dispatch.json', json.dumps(configuration))
            process = subprocess.run([executable, str(path), str(config), mode],
                                     capture_output=True, timeout=30)
            count += 1
            actual = process.stdout.decode('utf-8').replace('\r\n', '\n')
            error = process.stderr.decode('utf-8').replace('\r\n', '\n')
            assert bool(process.returncode) == fail, (name, mode, process.returncode, actual, error)
            assert actual == expected, (name, mode, actual, expected, error)
            marker = next((line for line in error.splitlines() if line.startswith('METRICS ')), None)
            assert marker, (name, error)
            metrics = json.loads(marker[8:])
            assert metrics['calls'] == calls, (name, mode, metrics, calls)
            assert metrics['retries'] == 0, (name, mode, metrics)
            if resolutions is not None:
                assert metrics['resolutions'] == resolutions, (name, mode, metrics, resolutions)
            if diagnostic:
                assert diagnostic in error, (name, mode, diagnostic, error)
            return metrics

        add = 'const add = func(a, b) { return a + b; }; '
        for mode in ['auto', 'required']:
            run('per-call', add + 'println(add("a", "b")); println(add(2, 3)); '
                'println(add("x", "y")); println(add(4, 5));',
                'ab\n5\nxy\n9\n', [spec(['integer', 'integer'])], 2, mode)
            run('arguments-once', add + 'var count = 0; '
                'const arg = func(v) { count += 1; return v; }; '
                'println(add(arg("a"), arg("b"))); println(count); '
                'println(add(arg(2), arg(3))); println(count);',
                'ab\n2\n5\n4\n', [spec(['integer', 'integer'])], 1, mode)
            run('extra-arguments', add + 'var count = 0; '
                'const side = func() { count += 1; return {}; }; '
                'println(add(2, 3, side())); println(count);',
                '5\n1\n', [spec(['integer', 'integer'])], 1, mode)
            run('missing-null', 'const f = func(a, b) { return b; }; println(f(7));',
                'null\n', [spec(['integer', 'null'], 'second', 'null')], 1, mode)
            run('no-specialization', add + 'println(add(2, 3));', '5\n', [], 0, mode)
            run('not-equal', 'const f = func(x) { return 100 + x; }; '
                'println(f(0)); println(f(2)); println(f(0)); println(f(-2));',
                '100\n2\n100\n-2\n',
                [spec(['integer'], 'first', conditions=[condition(0, 'notEqual', 0)])], 2, mode)
            run('disjoint', 'const f = func(x) { return 100 + x; }; '
                'println(f(-1)); println(f(0)); println(f(1));', '-100\n100\n200\n',
                [spec(['integer'], 'negative', sid=1, conditions=[condition(0, 'less', 0)]),
                 spec(['integer'], 'positive', sid=2, conditions=[condition(0, 'greater', 0)])],
                2, mode)
            run('same-types-different-return', 'const f = func(x) { return x; }; '
                'println(f(1)); println(f("safe"));', '1\nsafe\n',
                [spec(['integer'], 'first'), spec(['string'], 'first', 'string', sid=2)], 2, mode)
            run('boolean', 'const f = func(x) { return x; }; println(f(true)); println(f(false));',
                'true\nfalse\n', [spec(['boolean'], 'first', 'boolean')], 2, mode)
            run('character', "const f = func(x) { return x; }; println(f('😀'));",
                '😀\n', [spec(['character'], 'first', 'character')], 1, mode)
            run('real', 'const f = func(x) { return x; }; println(f(1.5));',
                '1.5\n', [spec(['real'], 'first', 'real')], 1, mode)
            run('exact-no-coercion', 'const f = func(x) { return x; }; '
                'println(f(1)); println(f(1.5)); println(f(true));', '1\n1.5\ntrue\n',
                [spec(['real'], 'first', 'real')], 1, mode)
            run('string-borrowed', 'const f = func(x) { return x; }; '
                'const a = f("путь😀"); const b = f("next"); println(a); println(b);',
                'путь😀\nnext\n', [spec(['string'], 'first', 'string')], 2, mode)
            run('string-host-allocation', 'const f = func() { return "VM"; }; '
                'const a = f(); const b = f(); println(a); println(b);',
                'A\x00😀\nA\x00😀\n', [spec([], 'string', 'string')], 2, mode)
            run('string-equality', 'const f = func(x) { return 10; }; '
                'println(f("other")); println(f("abc"));', '10\n200\n',
                [spec(['string'], 'positive', conditions=[condition(0, 'equal', 'abc', 'string')])],
                1, mode)
            run('string-zero-guard', 'const f = func(x) { return 10; }; '
                'println(f("a")); println(f("a\\0b")); println(f("a\\0c"));', '10\n200\n10\n',
                [spec(['string'], 'positive', conditions=[condition(0, 'equal', 'a\0b', 'string')])],
                1, mode)
            run('empty-string', 'const f = func(x) { return x; }; println(f(""));', '\n',
                [spec(['string'], 'first', 'string')], 1, mode)
            run('ordinary-object-no-conversion', 'var count = 0; '
                'const f = func(x) { return 7; }; '
                'const value = { public toString = func() { count += 1; return "oops"; }; }; '
                'println(f(value)); println(count);', '7\n0\n',
                [spec(['string'], 'positive')], 0, mode)
            run('closure-instances', 'const make = func(x) { return func() { return x; }; }; '
                'const a = make(7); const b = make(9); println(a()); println(b());',
                '7\n9\n', [], 0, mode)
            # A copied/flattened method has redirected captures and cannot inherit the original binding.
            run('transferred-method', 'const lib = include "holder.goat"; const original = lib.original; '
                'const copy = original.clone(); const flat = original.flat(); '
                'println(copy.next()); println(flat.next()); '
                'const detached = copy.next; println(detached()); println(original.value);',
                '1\n1\n2\n0\n', [spec([], 'positive')], 0, mode,
                files={'holder.goat': 'public original = { public var value = 0; '
                       'public next = func() { value += 1; return value; }; };'})
            for action in ['bad-tag', 'bad-reserved', 'bad-bool', 'bad-char',
                           'bad-string', 'status-mismatch', 'status-resource', 'status-unknown']:
                result_type = 'boolean' if action == 'bad-bool' else (
                    'character' if action == 'bad-char' else (
                        'string' if action == 'bad-string' else 'integer'))
                run(action, 'const f = func() { println("VM_REPLAY"); return 7; }; '
                    'f(); println("AFTER");', '', [spec([], action, result_type)], 1, mode, True,
                    diagnostic='Module source@0.0.0, function 1: native')
            run('module-method', 'const lib = include "lib.goat"; println(lib.add(2, 3));',
                '5\n', [spec(['integer', 'integer'])], 1, mode,
                files={'lib.goat': 'public add = func(a, b) { return a + b; };'})
            run('initializer-terminal', 'try { include "bad.goat"; } '
                'catch(e) { println("CAUGHT"); } println("AFTER");', '',
                [spec([], 'bad-tag')], 1, mode, True, diagnostic='Module source@0.0.0, function 1: native',
                files={'bad.goat': 'const f = func() { println("VM_REPLAY"); return 7; }; f();'})
            run('worker-terminal', 'const f = func() { println("VM_REPLAY"); return 7; }; '
                'const t = spawn(f); try { join(t); } catch(e) { println("CAUGHT"); } '
                'println("AFTER");', '', [spec([], 'bad-tag')], 1, mode, True,
                diagnostic='Module source@0.0.0, function 1: native')
            run('failed-file-before-native', 'var saved = null; public const sink = func(f) { saved = f; }; '
                'try { include "bad.goat"; } catch(e) { println(e); } '
                'try { saved(); } catch(e) { println(e); }',
                'init-failed\ninit-failed\n', [spec([], 'positive', fid=2)], 0, mode,
                files={'bad.goat': 'const root = include "main.goat"; '
                       'root.sink(func() { return 7; }); throw "init-failed";'})
        run('overlap', 'const f = func(x) { return x; }; println("NEVER");', '',
            [spec(['integer'], 'first', conditions=[condition(0, 'notEqual', 0)]),
             spec(['integer'], 'first', sid=2, conditions=[condition(0, 'greater', 0)])],
            0, binding_failure=True)
        run('unknown-function', 'const f = func(x) { return x; }; println("NEVER");', '',
            [spec(['integer'], 'first', fid=99)], 0, binding_failure=True)
        run('wrong-arity', 'const f = func(x) { return x; }; println("NEVER");', '',
            [spec(['integer', 'integer'])], 0, binding_failure=True)
        run('off', add + 'println(add(2, 3));', '5\n', [spec(['integer', 'integer'])], 0, 'off')
        run('load-failure-auto', add + 'println(add(2, 3)); println(add(4, 5));',
            '5\n9\n', [spec(['integer', 'integer'])], 0, resolver_failure=True, resolutions=1)
        run('load-failure-required', add + 'println(add(2, 3)); println("AFTER");',
            '', [spec(['integer', 'integer'])], 0, 'required', True, resolver_failure=True, resolutions=1,
            diagnostic='Module source@0.0.0, function 1: selected native library or ABI')
        run('unused-does-not-resolve', add + 'println("unused");', 'unused\n',
            [spec(['integer', 'integer'])], 0, 'required', resolver_failure=True, resolutions=0)
    identity = subprocess.run([executable, '--identity'], capture_output=True, timeout=30)
    assert identity.returncode == 0 and identity.stdout.strip() == b'OK', (identity.stdout, identity.stderr)
    count += 1
    print('Module dispatch checks: %d passed' % count)


if __name__ == '__main__':
    main()
