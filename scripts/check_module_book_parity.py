#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Temporary #392 gate: execute the untouched Python and independent C checks.

Both implementations read the same real checkout and use the same tools, but
create their own workspaces and derive all snippets from the actual documents.
CI invokes this wrapper under the CTest watchdog. Direct diagnostic invocation
relies only on each checker's own command deadlines.
Retire this wrapper only after the complete 14-context CI parity gate is green.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys

ORIGINAL_SHA256 = '2109f474f495ae6c8b997028296ce62cbf8cb76b9381d967d945f49867214ee0'
EXPECTED = ('Module book checks passed: 15 commands, matching RU/EN README/book '
            'tutorial and local links\n')


def main():
    if len(sys.argv) != 7:
        raise SystemExit('usage: check_module_book_parity.py GOAT INSPECT MERGE RUNNER BUILD SOURCE_ROOT')
    goat, inspect, merge, runner, build, root = map(lambda p: Path(p).resolve(), sys.argv[1:])
    original = root / 'scripts/check_module_book.py'
    if hashlib.sha256(original.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        raise SystemExit('original book checker bytes differ from base 8b2550a')
    if inspect != goat.with_name('goat-inspect' + goat.suffix) or merge != goat.with_name('goat-merge' + goat.suffix):
        raise SystemExit('original and C must use the same adjacent goat/inspect/merge tools')
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    # The original uses assertions. Explicitly ignore caller optimization flags.
    environment.pop('PYTHONOPTIMIZE', None)
    commands = [
        ('original', [sys.executable, str(original), str(goat)]),
        ('C', [str(runner), 'integration.module_book', str(goat), str(inspect),
               str(merge), str(build), str(root)]),
    ]
    # Preserve each checker's own child-process deadlines and tree cleanup.
    # The outer CTest watchdog bounds this pair without a competing direct-child kill.
    failures = []
    for label, command in commands:
        print('module-book parity: independently running ' + label, flush=True)
        try:
            result = subprocess.run(command, cwd=root, env=environment,
                                    input=b'', capture_output=True)
        except OSError as error:
            failures.append(label + ': ' + str(error))
            continue
        sys.stdout.buffer.write(result.stdout)
        sys.stderr.buffer.write(result.stderr)
        sys.stdout.flush()
        sys.stderr.flush()
        if result.returncode != 0:
            failures.append(label + ': exit ' + str(result.returncode))
        elif label == 'original' and result.stdout.decode('utf-8').replace('\r\n', '\n') != EXPECTED:
            failures.append('original: unexpected scenario completion summary')
    if failures:
        raise SystemExit('\n'.join(failures))
    print('module-book parity passed: independent original/C checks on identical checkout and tools')


if __name__ == '__main__':
    main()
