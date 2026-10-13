#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Exercise complete REPL sessions through redirected stdin on Linux and Windows."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    goat = Path(sys.argv[1]).resolve()
    env = dict(os.environ, GOAT_LANGUAGE='en')
    passed = 0

    def run(source, *options):
        result = subprocess.run([str(goat), '--interactive', *map(str, options)],
                                input=source.encode('utf-8'), capture_output=True,
                                env=env, timeout=20)
        stdout = result.stdout.decode('utf-8').replace('\r\n', '\n')
        stderr = result.stderr.decode('utf-8').replace('\r\n', '\n')
        return result.returncode, stdout, stderr

    def check(name, source, output, errors=(), options=()):
        nonlocal passed
        code, stdout, stderr = run(source, *options)
        assert code == 0, (name, code, stdout, stderr)
        # Redirected input is not echoed; prompts precede output or occupy empty lines.
        actual = '\n'.join(line for line in
                           (line.removeprefix('> ').removeprefix('  ')
                            for line in stdout.splitlines()) if line)
        assert actual == output, (name, actual, output)
        assert len(stderr.splitlines()) == len(errors), (name, stderr)
        for expected, line in zip(errors, stderr.splitlines()):
            assert expected in line, (name, expected, line)
        passed += 1
        print('[ ok ]', name)

    check('previous-line updates and for loop',
          'var x=1;\nx++;\nprintln(x=3);\nfor(;x<5;x++) println(x);\nprintln(x);\n',
          '3\n3\n4\n5')
    check('implicit constant remains immutable',
          'x=1;\nx=2;\nprintln(x);\n', '1', ['PROPERTY_IS_CONSTANT'])
    check('explicit constant remains immutable',
          'const x=7;\nx++;\nprintln(x);\n', '7', ['PROPERTY_IS_CONSTANT'])
    check('closure identity after an exception',
          'const f=func(){return y;};\nthrow 1;\nvar y=42;\nprintln(f());\n',
          '42', ['Uncaught exception: 1'])
    check('repeated exceptions and captured writes',
          'var x=1; const f=func(){x=x+1; return x;};\n'
          'throw 0;\nprintln(f());\nthrow 1;\nprintln(f());\n',
          '2\n3', ['Uncaught exception: 0', 'Uncaught exception: 1'])
    check('nested closure survives collection',
          'const make=func(n){return func(){return n;};};\n'
          'const f=make(42);\nthrow 1;\nprintln(f());\n',
          '42', ['Uncaught exception: 1'])
    check('exception inside a call preserves session',
          'var x=1; const f=func(){var t=3; throw t;};\nf();\nx++;\nprintln(x);\n',
          '2', ['Uncaught exception: 3'])
    check('syntax error recovery',
          'var x=1;\nconst broken = ;\nx++;\nprintln(x);\n',
          '2', ["'<interactive>', 2.16:"])
    check('blank lines count in diagnostics', '\n\n@\nprintln(9);\n',
          '9', ["'<interactive>', 3.1:"])
    check('continuation diagnostics and recovery',
          'println(\\\nmissing + );\nprintln(9);\n',
          '9', ["'<interactive>', 2.9:"])
    check('continuation keeps line comment boundaries',
          'var x=1; // comment \\\nx=2;\nprintln(x);\n', '2')
    check('multiline function',
          'const f=func(n){ \\\nreturn n+1; \\\n};\nprintln(f(4));\n', '5')
    check('Unicode session names and values',
          'var имя="Привет 😀";\nprintln(имя);\n', 'Привет 😀')
    check('accumulated bytecode retains old function addresses',
          'const f=func(n){return n+1;};\n' +
          ''.join(f'var v{i}={i};\n' for i in range(100)) + 'println(f(5));\n', '6')
    check('EOF without newline', 'println(8);', '8')
    check('empty input', '', '')
    check('explicit optimization none', 'var x=1;\nx++;\nprintln(x);\n', '2',
          options=('--optimize', 'none'))
    code, stdout, stderr = run('print("x");\nprintln("y");\n')
    assert (code, stdout, stderr) == (0, '> x\n> y\n> ', ''), (code, stdout, stderr)
    passed += 1
    print('[ ok ] prompts begin on a fresh line')

    with tempfile.TemporaryDirectory(prefix='goat-repl-') as directory:
        artifact = Path(directory) / 'unexpected.svg'
        conflicts = [('--compile',), ('--run',), ('--print-c',), ('--save-c',),
                     ('--save-library',), ('--print-source-code',), ('--print-analysis',),
                     ('--save-analysis', artifact), ('--save-graph', artifact),
                     ('--print-native',), ('--save-native', artifact),
                     ('--native', 'required')]
        for options in conflicts:
            code, stdout, stderr = run('println("must not run");\n', *options)
            assert code != 0 and not stdout, (options, code, stdout, stderr)
            assert 'The --interactive mode cannot be combined' in stderr, (options, stderr)
            assert not artifact.exists(), options
            passed += 1
            print('[ ok ] reject', options[0])
    print(f'Interactive testing done; total: {passed}, passed: {passed}, failed: 0')


if __name__ == '__main__':
    main()
