#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Top-level return and exit across dynamic file contexts (decisions 76–85)."""
from pathlib import Path
import subprocess
import sys
import tempfile

from module_execution_support import ExecutionSuite, put


with tempfile.TemporaryDirectory(prefix='goat-module-exit-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)
    goat = str(Path(sys.argv[2]).resolve())

    def invoke(project, command, expected, status=0, diagnostic=None):
        result = subprocess.run(command, cwd=suite.unrelated, env=project.environment,
                                capture_output=True, timeout=60)
        suite.count += 1
        actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
        assert result.returncode == status, (command, result.returncode, status,
                                              actual, result.stderr)
        assert actual == expected, (command, actual, expected, result.stderr)
        if diagnostic is None:
            assert not result.stderr, (command, result.stderr)
        else:
            assert diagnostic.encode() in result.stderr, (command, result.stderr)

    def program(name, code, expected, status=0, files=None, source=True, diagnostic=None):
        project = suite.project(name)
        archive = project.compile('app', code, files=files)
        invoke(project, [suite.exe, 'binary', str(archive)], expected, status, diagnostic)
        # Exercise the public CLI too, including real multi-file source and
        # retained ZIP snapshots, not just the component execution entry point.
        invoke(project, [goat, '--native', 'off', str(archive)], expected, status, diagnostic)
        descriptor = project.root / 'sources/app-1/module.gmod'
        invoke(project, [goat, '--native', 'off', str(descriptor)], expected, status, diagnostic)
        if source:
            path = project.root / 'sources/app-1/main.goat'
            configurations = [('none', 'off'), ('all', 'off')]
            if name in ['normal-no-unwind', 'exit-indirect-expression']:
                configurations.append(('all', 'auto'))
            for optimization, native in configurations:
                invoke(project, [goat, '--native', native, '--optimize', optimization, str(path)],
                       expected, status, diagnostic)
        return project

    # Every valid status retains exact OS identity; no modulo, sign conversion or
    # distinction between zero termination and ordinary completion is permitted.
    for status in [0, 1, 7, 127, 128, 254, 255]:
        program('status-' + str(status), 'print("buffered"); exit(' + str(status) +
                '); println("unreachable");', 'buffered', status)

    # Normal exit cannot be caught or overridden by an enclosing return/finally.
    program('normal-no-unwind',
            'const stop = func() { try { try { print("stop"); exit(7); } '
            'catch(e) { println("caught"); } finally { println("inner"); } } '
            'finally { println("outer"); return 99; } }; '
            'try { stop(); } finally { println("main"); } println("after");', 'stop', 7)
    program('exit-in-finally',
            'try { throw "pending"; } finally { print("finally"); exit(9); } '
            'println("after");', 'finally', 9)

    program('exit-indirect-expression',
            'const stop = exit; const after = func() { println("unreachable"); return 1; }; '
            'const consume = func(a,b) { println("unreachable"); }; '
            'consume(stop(17), after()); println("unreachable");', '', 17)

    program('exit-method',
            'const holder = { public stop = exit; }; holder.stop(19); '
            'println("unreachable");', '', 19)
    program('exit-shadowing',
            'const exit = func(code) { return code + 1; }; println(exit(7));', '8\n')
    program('caught-invalid-function',
            'const f = func() { try { exit(256); } catch(e) { '
            'println(e == Exceptions.INVALID_ARGUMENT); } return 42; }; println(f());',
            'true\n42\n')
    program('exit-replaces-return',
            'const f = func() { try { return 42; } finally { print("finally"); exit(11); } }; '
            'println(f());', 'finally', 11)

    # The legacy single-image CLI artifact and interactive entry also preserve
    # exact status instead of collapsing all nonzero results to generic failure.
    p = suite.project('cli-entry-points')
    source_path = put(p.root / 'main.goat', 'print("binary"); exit(7); println("unreachable");')
    invoke(p, [goat, '--compile', '--native', 'off', str(source_path)], '')
    invoke(p, [goat, '--run', '--native', 'off', str(source_path.with_suffix('.gbin'))],
           'binary', 7)
    # Explicit exit never resumes host-side report writing or destruction.
    invoke(p, [goat, '--save-native', str(p.root / 'missing/report'), str(source_path)],
           'binary', 7)
    result = subprocess.run([goat, '--interactive'], input=b'exit(7);\nprintln("unreachable");\n',
                            capture_output=True, timeout=60, cwd=suite.unrelated)
    suite.count += 1
    assert result.returncode == 7 and b'unreachable' not in result.stdout, (
        result.returncode, result.stdout, result.stderr)
    assert not result.stderr, result.stderr

    # Validate all argument properties before requesting termination, including
    # bool/character/real values that must not be coerced to integers.
    invalid = ['-1', '256', '9223372036854775807', '-9223372036854775807',
               '"7"', '7.0', '7.5', 'true', 'false', 'null', "'a'", '{}',
               'func() {}', '', '0, 1']
    for index, arguments in enumerate(invalid):
        program('invalid-' + str(index),
                'try { exit(' + arguments + '); println("unreachable"); } '
                'catch(e) { println(e == Exceptions.INVALID_ARGUMENT); } '
                'finally { println("finally"); } println("after");',
                'true\nfinally\nafter\n')
    program('argument-evaluation',
            'const argument = func() { println("argument"); throw "value"; }; '
            'try { exit(argument()); } catch(e) { println(e); } '
            'finally { println("finally"); } println("after");',
            'argument\nvalue\nfinally\nafter\n')

    # Included initializer catches a normal exception and can finish successfully.
    # The very same function is subsequently callable as ordinary program code.
    program('include-caught-then-exit',
            'const library = include "library.goat"; println("ready"); '
            'try { library.stop(); } finally { println("unreachable"); }',
            'true\ninitializer finally\nready\n', 7, files={
                'library.goat': 'public stop = func() { exit(7); }; '
                    'try { stop(); } catch(e) { '
                    'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
                    'finally { println("initializer finally"); }'})

    # Restriction follows active execution, not lexical __main__ or function origin.
    program('callback-into-main',
            'public stop = func() { println(__main__); exit(7); }; '
            'include "helper.goat"; println("ready"); stop();',
            'true\ntrue\nhelper finally\nready\ntrue\n', 7, files={
                'helper.goat': 'const root = include "main.goat"; '
                    'try { root.stop(); } catch(e) { '
                    'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
                    'finally { println("helper finally"); }'})

    # Escaping forbidden exit is cached just like any other initializer failure:
    # side effects happen once and re-entry throws the same exception object.
    program('include-sticky-failure',
            'var saved = null; try { include "bad.goat"; } catch(e) { saved = e; '
            'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
            'try { include "bad.goat"; } catch(e) { println(e == saved); } '
            'finally { println("caller finally"); } println("after");',
            'once\ninitializer finally\ntrue\ntrue\ncaller finally\nafter\n', files={
                'bad.goat': 'println("once"); try { exit(0); } '
                            'finally { println("initializer finally"); }'})

    # Arguments are still validated while initialization is active.
    program('initializer-invalid-argument',
            'include "helper.goat"; println("after");', 'true\ntrue\nafter\n', files={
                'helper.goat': 'try { exit(256); } catch(e) { '
                    'println(e == Exceptions.INVALID_ARGUMENT); } '
                    'try { exit(7); } catch(e) { '
                    'println(e == Exceptions.EXIT_DURING_INITIALIZATION); }'})

    program('initializer-finally-replaces-pending',
            'try { include "bad.goat"; } catch(e) { '
            'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } println("after");',
            'initializer finally\ntrue\nafter\n', files={
                'bad.goat': 'try { throw "original"; } finally { '
                            'println("initializer finally"); exit(7); }'})

    p = suite.project('import-caught-then-exit')
    library = p.compile('library',
        'external stop = func() { exit(255); }; '
        'if (!__main__) { try { stop(); } catch(e) { '
        'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
        'finally { println("import finally"); } } else { print("direct"); stop(); }')
    invoke(p, [suite.exe, 'binary', str(library)], 'direct', 255)
    app = p.compile('app', 'const lib = import "library@1"; println("ready"); '
                    'try { lib.stop(); } catch(e) { println("unreachable"); } '
                    'finally { println("unreachable"); }')
    invoke(p, [suite.exe, 'binary', str(app)], 'true\nimport finally\nready\n', 255)

    p = suite.project('import-sticky-failure')
    p.compile('bad', 'println("once"); try { exit(7); } '
                    'finally { println("import finally"); }')
    p.run('var saved = null; try { import "bad@1"; } catch(e) { saved = e; '
          'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
          'try { import "bad@1"; } catch(e) { println(e == saved); } '
          'finally { println("caller finally"); } println("after");',
          'once\nimport finally\ntrue\ntrue\ncaller finally\nafter\n')

    # A successfully initialized dependency must not evade the currently active
    # outer initializer; after that outer initializer completes, exit is normal.
    p = suite.project('transitive-dynamic-context')
    p.compile('leaf', 'external stop = func() { exit(23); };')
    p.compile('outer', 'const leaf = import "leaf@1"; external stop = leaf.stop; '
              'try { stop(); } catch(e) { '
              'println(e == Exceptions.EXIT_DURING_INITIALIZATION); } '
              'finally { println("outer finally"); }')
    app = p.compile('app', 'const leaf = import "leaf@1"; const outer = import "outer@1"; '
                    'println("ready"); outer.stop(); println("unreachable");')
    invoke(p, [suite.exe, 'binary', str(app)], 'true\nouter finally\nready\n', 23)

    program('initializer-uncaught', 'include "bad.goat"; println("unreachable");',
            'initializer finally\n', 1, files={
                'bad.goat': 'try { exit(7); } finally { println("initializer finally"); }'},
            diagnostic='EXIT_DURING_INITIALIZATION')
    program('top-return-uncaught', 'return; println("unreachable");', '', 1,
            diagnostic='RETURN_OUTSIDE_FUNCTION')

    # Return outside a function is runtime failure, including inside a top-level
    # try. Imported return is sticky unless its own initializer catches it.
    for keyword in ['return;', 'return 42;']:
        label = 'value' if '42' in keyword else 'bare'
        program('top-return-' + label,
                'try { ' + keyword + ' } catch(e) { '
                'println(e == Exceptions.RETURN_OUTSIDE_FUNCTION); } '
                'finally { println("finally"); } println("after");',
                'true\nfinally\nafter\n')
        p = suite.project('import-return-' + label)
        p.compile('bad', 'println("once"); try { ' + keyword + ' } '
                  'finally { println("import finally"); }')
        p.run('var saved = null; try { import "bad@1"; } catch(e) { saved = e; '
              'println(e == Exceptions.RETURN_OUTSIDE_FUNCTION); } '
              'try { import "bad@1"; } catch(e) { println(e == saved); } '
              'finally { println("caller finally"); } println("after");',
              'once\nimport finally\ntrue\ntrue\ncaller finally\nafter\n')

print(f'Module return/exit checks passed: {suite.count}')
