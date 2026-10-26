#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Production VM threads, dynamic initializer restrictions and shared lifetime."""
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

from module_execution_support import ExecutionSuite, put

with tempfile.TemporaryDirectory(prefix='goat-module-threads-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)
    suffix = '.exe' if sys.platform == 'win32' else ''
    lifetime = Path(sys.argv[1]).resolve().with_name('module_thread_lifetime_testing' + suffix)
    source = put(Path(temporary) / 'lifetime/main.goat',
                 'bootstrap(); public value = 42; public f = func() { '
                 'const c = include "child.goat"; return c.value; };')
    put(source.parent / 'child.goat', 'public value = 17;')
    checked = subprocess.run([str(lifetime), str(source)], capture_output=True, timeout=30)
    assert checked.returncode == 0 and checked.stdout.strip() == b'OK', (
        checked.returncode, checked.stdout, checked.stderr)
    suite.count += 1
    goat = str(Path(sys.argv[2]).resolve())

    def program(name, code, expected, files=None, cli=True):
        p = suite.project(name)
        p.run(code, expected, files=files)
        if not files and cli:
            source = p.root / 'sources/app-1/main.goat'
            for optimization, native in [('none', 'off'), ('all', 'off'), ('all', 'auto')]:
                command = [goat, '--optimize', optimization, '--native', native, str(source)]
                result = subprocess.run(command, capture_output=True, timeout=60,
                                        cwd=suite.unrelated, env=p.environment)
                suite.count += 1
                actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
                assert result.returncode == 0 and actual == expected, (
                    name, command, result.returncode, actual, result.stderr)
        if name.startswith('pending-direct-name'):
            source = p.root / 'sources/app-1/main.goat'
            for optimization in ['none', 'all']:
                source.with_suffix('.gbin').unlink(missing_ok=True)
                compiled = subprocess.run([goat, '--compile', '--optimize', optimization,
                                           '--native', 'off', str(source)],
                                          capture_output=True, timeout=60, env=p.environment)
                assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
                executed = subprocess.run([goat, '--run', '--native', 'off',
                                           str(source.with_suffix('.gbin'))],
                                          capture_output=True, timeout=60, env=p.environment)
                actual = executed.stdout.decode('utf-8').replace('\r\n', '\n')
                assert executed.returncode == 0 and actual == expected, (
                    executed.returncode, actual, executed.stderr)
                suite.count += 2
        return p

    program('main-ready-function',
            'const f = func(x) { return x + 1; }; const t = spawn(f, 41); '
            'println(join(t)); println(join(t)); println("main-end");',
            '42\n42\nmain-end\n')
    program('ordinary-helper',
            'const f = func() { const t = spawn(func() { return 13; }); return join(t); }; '
            'println(f());', '13\n')
    program('ready-main-value',
            'var value = 17; const t = spawn(func() { return value; }); println(join(t));', '17\n')
    # join makes the worker access happen before the later declaration, without delays.
    program('pending-direct-name-var-read',
            'const f = func() { return late; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } var late = 17; '
            'println(join(spawn(f)));', 'pending\n17\n')
    program('pending-direct-name-var-write',
            'const f = func() { late = 99; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } var late = 17; '
            'join(spawn(f)); println(late);', 'pending\n99\n')
    program('pending-direct-name-const-read',
            'const f = func() { return late; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } const late = 17; '
            'println(join(spawn(f)));', 'pending\n17\n')
    program('pending-direct-name-read',
            'public const f = func() { return late; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } public var late = 17; '
            'println(join(spawn(f)));', 'pending\n17\n')
    program('pending-direct-name-write',
            'public const f = func() { late = 99; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } public var late = 17; '
            'join(spawn(f)); println(late);', 'pending\n99\n')
    program('pending-main-read',
            'const f = func() { const root = include "main.goat"; return root.late; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } public var late = 17; '
            'println(join(spawn(f)));', 'pending\n17\n', cli=False)
    program('pending-main-write',
            'const f = func() { const root = include "main.goat"; root.late = 99; }; const t = spawn(f); '
            'try { join(t); } catch(e) { println("pending"); } public var late = 17; '
            'join(spawn(f)); println(late);', 'pending\n99\n', cli=False)
    program('thread-exception-identity',
            'const error = { public var code = 7; }; '
            'const t = spawn(func() { throw error; }); '
            'try { join(t); } catch(e) { println(e == error); e.code = 8; } '
            'try { join(t); } catch(e) { println(e.code); }', 'true\n8\n')
    program('thread-invalid-arguments',
            'try { spawn(42); } catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
            'try { join(-1); } catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
            'try { join(99999999); } catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
            'println(join(spawn(func() { return 4; })));', 'true\ntrue\ntrue\n4\n')
    program('descendant-lifetime',
            'const f = func() { return 42; }; '
            'spawn(func() { spawn(func() { println(f()); }); });', '42\n')
    program('nested-joins',
            'const child = spawn(func() { return join(spawn(func() { return 37; })); }); '
            'println(join(child));', '37\n')
    program('include-restrictions-and-restore',
            'include "init.goat"; println(join(spawn(func() { return 42; })));',
            'true\ntrue\n42\n', files={'init.goat':
                'try { spawn(func() {}); } catch(e) { println(e == Exceptions.INVALID_OPERATION); } '
                'try { join(9999); } catch(e) { println(e == Exceptions.INVALID_OPERATION); }'})
    program('include-escaped-error-restore',
            'var saved = null; try { include "bad.goat"; } catch(e) { saved = e; } '
            'println(join(spawn(func() { return 7; }))); '
            'try { include "bad.goat"; } catch(e) { println(e == saved); } '
            'println(join(spawn(func() { return 8; })));', 'once\n7\ntrue\n8\n',
            files={'bad.goat': 'println("once"); spawn(func() {});'})
    program('ready-helper-keeps-restrictions',
            'const helper = include "helper.goat"; include "init.goat"; println(helper.work());',
            'true\ntrue\n25\n', files={
                'helper.goat': 'public work = func() { return join(spawn(func() { return 25; })); }; '
                               'public wait = func(id) { return join(id); };',
                'init.goat': 'const h = include "helper.goat"; '
                    'try { h.work(); } catch(e) { println(e == Exceptions.INVALID_OPERATION); } '
                    'try { h.wait(9999); } catch(e) { println(e == Exceptions.INVALID_OPERATION); }'})
    program('nested-include-keeps-outer-restriction',
            'include "outer.goat"; println(join(spawn(func() { return 9; })));',
            'true\ntrue\n9\n', files={
                'inner.goat': 'public value = 1;',
                'outer.goat': 'include "inner.goat"; '
                    'try { spawn(func() {}); } catch(e) { println(e == Exceptions.INVALID_OPERATION); } '
                    'try { join(999); } catch(e) { println(e == Exceptions.INVALID_OPERATION); }'})
    program('cyclic-include-with-owner',
            'const a = include "a.goat"; println(a.value); '
            'println(join(spawn(func() { return a.value; })));', 'pending\n42\n42\n', files={
                'a.goat': 'include "b.goat"; public value = 42;',
                'b.goat': 'const a = include "a.goat"; try { a.value; } catch(e) { println("pending"); }'})
    p = suite.project('imports-dynamic-and-lifetime')
    p.compile('helper', 'external work = func() { return join(spawn(func() { return 31; })); };')
    p.compile('outer', 'const helper = import "helper@1"; '
              'try { helper.work(); } catch(e) { println(e == Exceptions.INVALID_OPERATION); } '
              'external value = 42;')
    p.run('const helper = import "helper@1"; const outer = import "outer@1"; '
          'println(helper.work()); '
          'spawn(func() { const module = import "outer@1"; '
          'spawn(func() { println(module.value); }); });', 'true\n31\n42\n')
    p = suite.project('concurrent-same-include')
    p.run('const f = func() { return include "library.goat"; }; '
          'const a = spawn(f); const b = spawn(f); println(join(a) == join(b));',
          'once\ntrue\n', files={'library.goat': 'println("once"); public value = 42;'})
    p = suite.project('concurrent-failed-include')
    p.run('const f = func() { try { return include "bad.goat"; } catch(e) { return e; } }; '
          'const a = spawn(f); const b = spawn(f); println(join(a) == join(b));',
          'once\ntrue\n', files={'bad.goat': 'println("once"); throw { public code = 42; };'})

    # Two host-pipe handshakes, not a delay: ready ordinary code must run while
    # the first initializer is blocked in input and still owns its global lock.
    p = suite.project('initializer-io-independent-worker')
    artifact = p.compile('app',
        'public var started = false; const ready = include "ready.goat"; '
        'const a = spawn(func() { include "blocked.goat"; }); '
        'const b = spawn(func() { while (!started) {} ready.work(); }); '
        'join(a); join(b); println("done");', files={
            'ready.goat': 'public work = func() { join(spawn(func() {})); '
                          'println("ready"); input(); };',
            'blocked.goat': 'const main = include "main.goat"; println("waiting"); '
                            'main.started = true; input();'})
    child = subprocess.Popen([suite.exe, 'binary', str(artifact)],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, cwd=suite.unrelated,
                             env=p.environment, text=True, encoding='utf-8')
    lines = queue.Queue()
    def read_lines():
        for line in child.stdout:
            lines.put(line.rstrip('\r\n'))
    reader = threading.Thread(target=read_lines, daemon=True)
    reader.start()
    try:
        assert lines.get(timeout=30) == 'waiting'
        assert lines.get(timeout=30) == 'ready'
        child.stdin.write('first\nsecond\n')
        child.stdin.flush()
        assert lines.get(timeout=30) == 'done'
        assert child.wait(timeout=30) == 0, child.stderr.read()
        suite.count += 1
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        child.stdin.close()
        reader.join(timeout=30)
        child.stdout.close()
        child.stderr.close()

print(f'Module threads: {suite.count} executions passed')
