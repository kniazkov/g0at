#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Native host exit services and interruption of noncooperative native workers."""
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix='goat-native-exit-') as temporary:
        root = Path(temporary)

        def run(name, code, signatures, expected='', status=0, files=None, terminal=False, exited=False, legacy=False):
            nonlocal checks
            directory = root / name
            directory.mkdir()
            source = directory / 'main.goat'
            source.write_text(code, encoding='utf-8')
            for filename, text in (files or {}).items():
                (directory / filename).write_text(text, encoding='utf-8')
            specs = []
            for index, (types, action, fid) in enumerate(signatures, 1):
                specs.append({'specializationId': index, 'functionId': fid,
                              'symbol': f'goat_fn_{index}_v1',
                              'signature': {'id': 'goat.adapter.v1', 'arguments': types,
                                            'result': 'integer'},
                              'when': {'argumentTypes': types}})
            config = directory / 'config.json'
            config.write_text(json.dumps({'specializations': specs,
                                          'actions': [s[1] for s in signatures],
                                          'legacyLibrary': str(library) if legacy else None}))
            result = subprocess.run([executable, str(source), str(config), 'required'],
                                    capture_output=True, timeout=15)
            checks += 1
            actual = result.stdout.decode().replace('\r\n', '\n')
            assert result.returncode == status, (name, result.returncode, result.stderr, actual)
            assert actual == expected, (name, actual, expected, result.stderr)
            if terminal:
                assert (b'METRICS ' if legacy else b'native adapter failed') in result.stderr, (name, result.stderr)
            elif status == 0 and not exited:
                assert b'METRICS ' in result.stderr, (name, result.stderr)
            else:
                # Native entry never returns to host cleanup or subsequent Goat code.
                assert not result.stderr, (name, result.stderr)

        result = subprocess.run([executable, '--exit-prefixes'], capture_output=True, timeout=10)
        assert result.returncode == 0 and result.stdout.strip() == b'OK', result
        checks += 1
        compiler = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('CC', 'gcc')
        compiler = shutil.which(compiler) or compiler
        repository = Path(__file__).resolve().parent.parent
        library = root / ('legacy.dll' if os.name == 'nt' else 'legacy.so')
        flags = ['-shared'] + ([] if os.name == 'nt' else ['-fPIC'])
        built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                '-pedantic-errors', '-O2', '-DGOAT_NATIVE_BUILD',
                                '-I' + str(repository / 'include'), *flags,
                                str(repository / 'test/functional/native_exit/provider.c'),
                                '-o', str(library)], capture_output=True, timeout=60)
        assert built.returncode == 0, (built.stdout, built.stderr)
        function = 'const stop = func(x) { return 100; }; '
        for code in [0, 7, 255]:
            run(f'code-{code}', function + f'print("before"); stop({code}); println("after");',
                [(['integer'], 'exit', 1)], 'before', code, exited=True)
        for index, (argument, type_) in enumerate([
                ('-1', 'integer'), ('256', 'integer'), ('7.0', 'real'),
                ('true', 'boolean'), ("'a'", 'character'), ('"7"', 'string')]):
            run(f'invalid-{index}', function + f'try {{ stop({argument}); }} '
                'catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
                'finally { println("finally"); } println("after");',
                [([type_], 'exit', 1)], 'true\nfinally\nafter\n')
        for count in [0, 2]:
            params = ','.join('a' + str(i) for i in range(count))
            args = ','.join('7' for _ in range(count))
            run(f'arity-{count}', f'const stop = func({params}) {{ return 1; }}; '
                f'try {{ stop({args}); }} catch(e) {{ println(e == Exceptions.INVALID_ARGUMENT); }}',
                [(['integer'] * count, 'exit', 1)], 'true\n')
        run('invalid-then-valid', function + 'try { stop(256); } catch(e) { print("caught"); } '
            'stop(7); println("after");', [(['integer'], 'exit', 1)], 'caught', 7)
        run('native-initializer', 'public stop = func(x) { return 100; }; '
            'include "init.goat"; print("ready"); stop(7);', [(['integer'], 'exit', 1)],
            'true\nfinally\nready', 7,
            {'init.goat': 'const root = include "main.goat"; try { root.stop(7); } '
             'catch(e) { println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
             'finally { println("finally"); }'})
        run('native-no-finally', function + 'try { stop(7); } catch(e) { print("bad"); } '
            'finally { print("bad"); }', [(['integer'], 'exit', 1)], '', 7)
        for action in ['exit-forged', 'exit-ignored']:
            run(action, function + 'try { stop(256); } catch(e) { print("caught"); } '
                'print("after");', [(['integer'], action, 1)], '', 1, terminal=True)
        for code in [0, 7, 255]:
            run(f'legacy-call-{code}', function + f'print("before"); stop({code}); print("bad");',
                [], 'before', code, exited=True, legacy=True)
        run('legacy-invalid', function + 'try { stop(256); } '
            'catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
            'finally { println("finally"); } println("after");',
            [], 'true\nfinally\nafter\n', legacy=True)
        run('legacy-initializer', 'public stop = func(x) { return 100; }; '
            'include "init.goat"; print("ready"); stop(7);', [],
            'true\nfinally\nready', 7, legacy=True,
            files={'init.goat': 'const root = include "main.goat"; const stop = root.stop; try { stop(7); } '
                   'catch(e) { println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
                   'finally { println("finally"); }'})
        for code in [257, 258]:
            run(f'legacy-forged-{code}', function + f'try {{ stop({code}); }} '
                'catch(e) { print("caught"); } print("after");', [], '', 1,
                legacy=True, terminal=True)
        for worker in ['loop', 'block']:
            source = ('const worker = func(x) { return 1; }; '
                      'const ready = func(x) { return 2; }; '
                      'const stop = func(x) { return 3; }; '
                      'spawn(worker, 1); ready(0); print("entered"); ')
            for path in ['vm', 'native']:
                run(worker + '-' + path, source +
                    ('exit(7);' if path == 'vm' else 'stop(7);') + 'print("after");',
                    [(['integer'], worker, 1), (['integer'], 'wait', 2),
                     (['integer'], 'exit', 3)], 'entered', 7)
    print(f'Native exit service scenarios passed: {checks}')


if __name__ == '__main__':
    main()
