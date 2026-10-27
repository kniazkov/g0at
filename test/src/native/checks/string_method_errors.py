#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Check string-method fallback, argument validation and callable identity."""
import importlib.util
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('native_runner', ROOT / 'scripts/check_native.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def main():
    exe = str(Path(sys.argv[1]).resolve())
    work = Path(sys.argv[2]).resolve()
    cases = {
        'bounds': ('''
const slice = func(s, n) { return s.repeat(2).substring(n); };
const character = func(s, n) { return "" + s.charAt(n); };
const copies = func(s, n) { return s.repeat(n); };
println(slice("ab", 1));
println(character("ab", 1));
println(copies("ab", 1));
try { slice("ab", -1); } catch(e) { println("slice negative: " + e); }
try { slice("ab", 5); } catch(e) { println("slice past end: " + e); }
try { character("ab", -1); } catch(e) { println("char negative: " + e); }
try { character("ab", 2); } catch(e) { println("char past end: " + e); }
try { copies("ab", -1); } catch(e) { println("repeat negative: " + e); }
try { copies("ab", 9223372036854775807); } catch(e) { println("repeat overflow: " + e); }
println(slice("ab", 4));
''', 10, 4, 6, 3),
        'types': ('''
const good = func(s) { return s.trim(); };
const bad = func(s, n) { return "" + s.contains(n); };
const missing = func(s) { return s.substring(); };
println(good(" ok "));
try { bad("abc", 1); } catch(e) { println("bad needle: " + e); }
try { missing("abc"); } catch(e) { println("missing index: " + e); }
''', 1, 1, 0, 1),
        'identity': ('''
trim = func(s) { return "shadow:" + s; };
const method = func(s) { return s.trim(); };
const plain = func(s) { return trim(s); };
println(method(" ok "));
println(plain(" ok "));
''', 1, 1, 0, 1),
    }
    for name, (source, attempts, succeeded, retries, bound) in cases.items():
        case_dir = work / name
        case_dir.mkdir()
        program = case_dir / 'program.goat'
        program.write_text(source, encoding='utf-8')

        def execute(label, args, env=None):
            result = runner.execute([exe, '--lang', 'en', *map(str, args)], case_dir, label, env)
            runner.require(not result.stderr, f'{name}/{label}: unexpected stderr')
            return result.stdout

        reference = execute('vm', ['--native', 'off', '--optimize', 'none', program])
        execute('compile', ['--compile', '--native', 'required', program])
        for saved in (False, True):
            label = 'saved' if saved else 'native'
            report = case_dir / (label + '.report')
            env = os.environ.copy()
            if saved:
                program.unlink()
                env['CC'] = str(case_dir / 'missing-compiler')
            args = ['--run'] if saved else []
            args += ['--native', 'required', '--save-native', report,
                     program.with_suffix('.gbin') if saved else program]
            actual = execute(label, args, env)
            runner.compare_output(actual, reference, f'{name}/{label} vs VM')
            fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
            expected = dict(mode='required', preparation='ready', bound=str(bound),
                            attempts=str(attempts), succeeded=str(succeeded), retries=str(retries))
            for key, value in expected.items():
                runner.require(fields.get(key) == value, f'{name}/{label}: {key}: {fields}')
        print(f'[ ok ] {name}')


if __name__ == '__main__':
    main()
