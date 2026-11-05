#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Temporary #393 parity: unchanged Python and C read identical frozen bytes."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) not in (5, 6) or (len(sys.argv) == 6 and sys.argv[5] != '--keep-work'):
        raise SystemExit('usage: check_graph_parity.py RUNNER BUILD DATA SCRIPT [--keep-work]')
    runner, build, data, script = (Path(value).resolve() for value in sys.argv[1:5])
    original = script.read_bytes().split(b"python3 - <<'PY'\n", 1)[1].split(b'\nPY\n', 1)[0] + b'\n'
    expected = '2431003535c1426296b9443abb221269f7f63bce36623348b9909b667581cde7'
    if hashlib.sha256(original).hexdigest() != expected:
        raise SystemExit('original Python SVG reference changed; parity refused')
    # Only write beneath BUILD; test/data stays read-only.
    if build == data or build in data.parents or data in build.parents:
        raise SystemExit('BUILD and DATA must be disjoint')
    work = Path(tempfile.mkdtemp(prefix='graph-parity-', dir=build))
    success = False
    try:
        inputs = work / 'build'
        inputs.mkdir()
        for name in ('fibonacci-analysis.svg', 'fibonacci-output.txt',
                     'replacements-analysis.svg', 'replacements-output.txt'):
            shutil.copyfile(data / name, inputs / name)
        reference = work / 'original.py'
        reference.write_bytes(original)
        environment = os.environ.copy()
        environment.pop('PYTHONOPTIMIZE', None)
        # Separate interpreter, without -O: the exact original assertions execute.
        subprocess.run([sys.executable, str(reference)], cwd=work, env=environment,
                       check=True, timeout=30)
        command = [str(runner), 'integration.graph', str(build), str(data), str(inputs)]
        if len(sys.argv) == 6:
            command.append('--keep-work')
        subprocess.run(command, check=True, timeout=180)
        success = True
    finally:
        if success and len(sys.argv) == 5:
            shutil.rmtree(work)
        else:
            print(f'Graph parity inputs retained: {work}', file=sys.stderr)
    print('Graph original/C parity passed')


if __name__ == '__main__':
    main()
