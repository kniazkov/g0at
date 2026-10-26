#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Execute the matching README/book module tutorial and validate local links.

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


def check_tutorial(path):
    """Match commands in each entry point to the tutorial executed below."""
    text = path.read_text(encoding='utf-8')
    blocks = re.findall(r'<!-- module-book-cli -->\s*```sh\n(.*?)```', text, re.S)
    assert len(blocks) == 1, (path, 'one marked runnable tutorial required')
    commands = [line.strip() for line in blocks[0].splitlines() if line.strip()]
    assert commands == COMMANDS, (path, 'tutorial drift', commands)


def check_links(path):
    """Check local file destinations; external URLs and anchors are not fetched."""
    text = path.read_text(encoding='utf-8')
    assert sum(line.startswith('```') for line in text.splitlines()) % 2 == 0, (
        path, 'unclosed fenced code block')
    # Code blocks illustrate text and URLs, not document navigation.
    prose = re.sub(r'```.*?```', '', text, flags=re.S)
    prose = re.sub(r'`[^`]*`', '', prose)
    for target in re.findall(r'\[[^\]\n]+\]\(([^\s)]+)\)', prose):
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target) or target.startswith('#'):
            continue
        filename = unquote(target.split('#', 1)[0])
        assert (path.parent / filename).exists(), (path, 'broken link', target)


def check_books():
    """Keep README/book CLI examples synchronized and documentation navigable."""
    pages = {}
    for language in ('ru', 'en'):
        pages[language] = {p.name for p in (BOOK / language).glob('*.md')}
        check_tutorial(BOOK / language / '30-modules.md')
        for path in (BOOK / language).glob('*.md'):
            check_links(path)
    assert pages['ru'] == pages['en'], ('unpaired book pages', pages)
    for filename in ('README.md', 'README.ru.md'):
        check_tutorial(ROOT / filename)
        check_links(ROOT / filename)


def readme_snippets():
    """Read the snippets actually displayed to users, never duplicate their code."""
    snippets = {}
    for marker in ('closure', 'loop'):
        pair = []
        for filename in ('README.md', 'README.ru.md'):
            text = (ROOT / filename).read_text(encoding='utf-8')
            blocks = re.findall(r'<!-- readme-' + marker +
                                r' -->\s*```(?:text|goat)\n(.*?)```', text, re.S)
            assert len(blocks) == 1, (filename, marker, 'one runnable snippet required')
            pair.append(blocks[0])
        assert pair[0] == pair[1], (marker, 'RU/EN source snippet drift')
        snippets[marker] = pair[0]
    return snippets


def metadata(path):
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read('module.json'))


check_books()
snippets = readme_snippets()
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

    for marker, source in snippets.items():
        path = root / (marker + '.goat')
        path.write_text(source, encoding='utf-8')
        run('goat ' + path.name + ' --native off',
            expected='5\n' if marker == 'closure' else '10\n')

    shutil.copyfile(ROOT / 'example/hello_world.goat', root / 'hello.goat')
    run('goat hello.goat', expected='it works!\n')
    shutil.copyfile(BOOK / 'examples/22-native-module.goat', root / 'program.goat')
    # These are the README's standalone saved-program commands. Unlike .gmod,
    # direct .goat defaults to VM-only compilation without a native compiler.
    run('goat --compile program.goat', expected='')
    (root / 'program.goat').unlink()
    run('goat --run program.gbin', expected='7\n6\n4.0\n')

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

print(f'Module book checks passed: {count} commands, matching RU/EN README/book tutorial and local links')
