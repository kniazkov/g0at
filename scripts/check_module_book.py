#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Execute the matching RU/EN module tutorial and validate local book links.

Usage: python scripts/check_module_book.py path/to/goat
The goat-inspect and goat-merge executables must be beside goat. The examples
are copied to a temporary directory: neither checkout sources nor published
archives are modified, and no native compiler is needed.
"""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import unquote
import zipfile

from inspect_report_test_support import schema_check

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'docs/book'
EXAMPLES = BOOK / 'examples/30-modules'
GOAT = Path(sys.argv[1]).resolve()
TOOLS = {name: str(GOAT.with_name(name + GOAT.suffix))
         for name in ('goat', 'goat-inspect', 'goat-merge')}
COMMANDS = [
    'goat build geometry.gmod --native off',
    'goat build app.gmod --native off',
    'goat run app.gmod --native off',
    'goat run modules/tutorial/1.0.gbin --module-dir modules --native off',
    'goat-inspect --json --module-dir modules modules/tutorial/1.0.gbin',
    'goat-merge merged.gbin modules/geometry/1.0.gbin',
]


def check_books():
    """Keep published CLI examples executable, synchronized, and discoverable."""
    pages = {}
    for language in ('ru', 'en'):
        pages[language] = {p.name for p in (BOOK / language).glob('*.md')}
        chapter = (BOOK / language / '30-modules.md').read_text(encoding='utf-8')
        blocks = re.findall(r'<!-- module-book-cli -->\s*```sh\n(.*?)```',
                            chapter, re.S)
        assert len(blocks) == 1, (language, 'one marked runnable tutorial required')
        commands = [line.strip() for line in blocks[0].splitlines() if line.strip()]
        assert commands == COMMANDS, (language, 'tutorial drift', commands)
        for path in (BOOK / language).glob('*.md'):
            text = path.read_text(encoding='utf-8')
            assert sum(line.startswith('```') for line in text.splitlines()) % 2 == 0, (
                path, 'unclosed fenced code block')
            # Code blocks illustrate text and URLs, not book navigation.
            prose = re.sub(r'```.*?```', '', text, flags=re.S)
            prose = re.sub(r'`[^`]*`', '', prose)
            for target in re.findall(r'\[[^\]\n]+\]\(([^\s)]+)\)', prose):
                if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target) or target.startswith('#'):
                    continue
                filename = unquote(target.split('#', 1)[0])
                assert (path.parent / filename).exists(), (path, 'broken link', target)
    assert pages['ru'] == pages['en'], ('unpaired book pages', pages)


def metadata(path):
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read('module.json'))


check_books()
count = 0
with tempfile.TemporaryDirectory(prefix='goat-book-') as temporary:
    root = Path(temporary) / 'module tutorial'
    shutil.copytree(EXAMPLES, root)
    environment = dict(os.environ, GOAT_MODULE_PATH='')

    def run(command, expected=None, fail=False):
        global count
        args = shlex.split(command)
        result = subprocess.run([TOOLS[args[0]], *args[1:]], cwd=root,
                                env=environment, input=b'', capture_output=True,
                                timeout=60)
        count += 1
        assert (result.returncode != 0) if fail else result.returncode == 0, (
            command, result.returncode, result.stdout, result.stderr)
        if expected is not None:
            assert result.stdout.decode('utf-8').replace('\r\n', '\n') == expected, (
                command, result.stdout, result.stderr)
        return result

    for index, command in enumerate(COMMANDS):
        result = run(command, expected='' if index < 2 else '25\n' if index < 4 else None)
        if index == 4:
            assert not result.stderr, result.stderr
            report = json.loads(result.stdout)
            schema_path = ROOT / 'docs/module-format-v1/inspect-report.schema.json'
            schema = json.loads(schema_path.read_text(encoding='utf-8'))
            schema_check(report, schema, schema, schema_path.parent)
            assert report['reportVersion'] == 1 and report['success']
            assert {m['metadata']['name'] for m in report['modules']} == {'tutorial', 'geometry'}
            assert all(m['validation'] == 'passed' and not m['native'] for m in report['modules'])

    geometry = root / 'modules/geometry/1.0.gbin'
    app = root / 'modules/tutorial/1.0.gbin'
    original = metadata(geometry)
    merged = metadata(root / 'merged.gbin')
    assert merged == original, (merged, original)
    assert len(original['files']) == 2, 'include must package both source files'
    assert 'square' in original['externals'], original
    assert metadata(app)['dependencies'] == [
        {'name': 'geometry', 'version': '1.0', 'hash': original['mainHash']}]

    # Ready distribution does not need the original sources or descriptions.
    shutil.rmtree(root / 'src')
    for descriptor in root.glob('*.gmod'):
        descriptor.unlink()
    run(COMMANDS[3], expected='25\n')
    # Dependencies remain external: inspect and execution must reject absence.
    geometry.unlink()
    run(COMMANDS[3], expected='', fail=True)
    result = run(COMMANDS[4], fail=True)
    report = json.loads(result.stdout)
    assert not report['success'] and report['exitCode'] != 0, report
    # Merge validates only its inputs and never resolves external dependencies.
    run('goat-merge standalone-copy.gbin modules/tutorial/1.0.gbin')
    assert metadata(root / 'standalone-copy.gbin') == metadata(app)

print(f'Module book checks passed: {count} commands, matching RU/EN tutorial and local links')
