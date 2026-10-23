#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Run the manifest-driven native functional suite against the unoptimized VM."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / 'test/native'


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def normalized(data):
    # Windows text output uses CRLF. Do not trim whitespace, NULs or empty lines.
    return data.replace(b'\r\n', b'\n')


def compare_output(actual, expected, label):
    require(normalized(actual) == normalized(expected), f'{label}: output differs; see saved stdout files')


def check_report(path, case, native=True):
    fields = dict(line.split('=', 1) for line in path.read_text(encoding='utf-8').splitlines())
    require(fields.get('mode') == ('required' if native else 'off'), f'{path}: wrong mode: {fields}')
    calls = case['calls'] if native else 0
    for key, expected in [('attempts', calls), ('succeeded', calls), ('retries', 0)]:
        require(fields.get(key) == str(expected), f'{path}: expected {key}={expected}: {fields}')
    if native:
        require(calls > 0, f'{path}: test must enter native code')
        require(fields.get('preparation') == 'ready', f'{path}: module not ready: {fields}')
        require(int(fields.get('bound', '0')) > 0, f'{path}: no bound functions: {fields}')
        if 'bound' in case:
            require(fields['bound'] == str(case['bound']), f'{path}: wrong bound function count: {fields}')


def execute(command, work, label, env=None):
    (work / (label + '.command.json')).write_text(json.dumps(command, ensure_ascii=False), encoding='utf-8')
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, env=env, timeout=300)
    except subprocess.TimeoutExpired as error:
        (work / (label + '.stdout')).write_bytes(error.stdout or b'')
        (work / (label + '.stderr')).write_bytes(error.stderr or b'')
        raise
    (work / (label + '.stdout')).write_bytes(result.stdout)
    (work / (label + '.stderr')).write_bytes(result.stderr)
    require(result.returncode == 0, f'{label}: exit {result.returncode}; see {work / (label + ".stderr")}')
    return result


def program_case(exe, case, work):
    fixture = SUITE / case['name']
    original = fixture / 'program.goat'
    source = work / 'program.goat'
    shutil.copyfile(original, source)
    if 'example' in case:
        compare_output(original.read_bytes(), (ROOT / case['example']).read_bytes(), 'example source')

    def goat(label, *args, env=None):
        result = execute([str(exe), '--lang', 'en', *map(str, args)], work, label, env)
        require(not result.stderr, f'{label}: unexpected stderr; see {work}')
        return result.stdout

    reference = goat('vm', '--optimize', 'none', '--native', 'off',
                     '--save-native', work / 'vm.report', source)
    check_report(work / 'vm.report', case, native=False)
    expected = fixture / 'expected_output.txt'
    if expected.exists():
        compare_output(reference, expected.read_bytes(), 'VM vs expected')
    for text in case.get('output_contains', []):
        require(text.encode('utf-8') in normalized(reference), f'VM output missing {text!r}')
    actual = goat('native', '--native', 'required', '--save-native', work / 'native.report', source)
    compare_output(actual, reference, 'native vs VM')
    check_report(work / 'native.report', case)
    if case.get('generated'):
        generated = goat('generated', '--print-c', source).decode('utf-8')
        for pattern, count in case['generated'].items():
            require(generated.count(pattern) >= count, f'missing generated C pattern {pattern!r}')
    if 'driver' in case:
        goat('export', '--save-c', source)
        driver = work / 'adapter.exe'
        execute([os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-DMEMORY_DEBUG', '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'),
                 str(fixture / case['driver']), str(source.with_suffix('.c')),
                 str(ROOT / 'src/model/native_services.c'), str(ROOT / 'src/lib/unicode.c'), str(ROOT / 'src/lib/allocate.c'),
                 str(work / 'libgoat_native.a'), '-lm', '-o', str(driver)], work, 'driver-build')
        execute([str(driver)], work, 'driver')
    compare_output(goat('compile', '--compile', '--native', 'required', source), b'', 'compile')
    source.unlink()
    environment = os.environ.copy()
    environment['CC'] = str(work / 'missing-compiler')
    persisted = goat('saved', '--run', '--native', 'required', '--save-native', work / 'saved.report',
                     source.with_suffix('.gbin'), env=environment)
    compare_output(persisted, reference, 'saved native vs VM')
    check_report(work / 'saved.report', case)


def load_cases():
    cases = json.loads((SUITE / 'list.json').read_text(encoding='utf-8'))
    require(isinstance(cases, list) and cases, 'native list must be a nonempty array')
    names = set()
    allowed = {'name', 'calls', 'bound', 'example', 'driver', 'generated', 'output_contains', 'check'}
    for case in cases:
        require(not (case.keys() - allowed), f'unknown fields: {case.keys() - allowed}')
        name = case['name']
        require(re.fullmatch(r'[a-z][a-z0-9_]*', name) and name not in names, f'invalid/duplicate name: {name}')
        names.add(name)
        if 'check' in case:
            require((SUITE / case['check']).is_file(), f'missing check: {case}')
        else:
            require(type(case.get('calls')) is int and case['calls'] > 0, f'positive calls required: {name}')
            require((SUITE / name / 'program.goat').is_file(), f'missing program: {name}')
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('interpreter', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', nargs='+', help='run selected manifest names')
    args = parser.parse_args()
    cases = load_cases()
    if args.only:
        unknown = set(args.only) - {case['name'] for case in cases}
        require(not unknown, f'unknown tests: {sorted(unknown)}')
        cases = [case for case in cases if case['name'] in args.only]
    exe = args.interpreter.resolve()
    require(exe.is_file(), f'missing interpreter: {exe}')
    args.output.mkdir(parents=True, exist_ok=True)
    # Keep diagnostics and avoid stale exported SDK files across repeated runs.
    run_dir = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    print(f'Native functional diagnostics: {run_dir}', flush=True)
    failed = 0
    for case in cases:
        work = run_dir / case['name']
        work.mkdir()
        try:
            if 'check' in case:
                execute([sys.executable, str(SUITE / case['check']), str(exe), str(work)], work, 'check')
            else:
                program_case(exe, case, work)
            print(f'[ ok ] {case["name"]}', flush=True)
        except (AssertionError, OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            failed += 1
            print(f'[fail] {case["name"]}: {ascii(str(error))}', flush=True)
    print(f'Native functional testing done; total: {len(cases)}, passed: {len(cases)-failed}, failed: {failed}')
    return bool(failed)


if __name__ == '__main__':
    sys.exit(main())
