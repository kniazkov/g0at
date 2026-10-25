#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compiled source diagnostics: immutable mappings, external files and imports (#234)."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

from module_execution_support import ExecutionSuite


def diagnostic(project, archive, *fragments, expected=''):
    result = project.invoke('binary', archive, fail=True, expected=expected)
    text = result.stderr.decode('utf-8').replace('\r\n', '\n')
    assert 'Uncaught:' in text, text
    for fragment in fragments:
        assert fragment in text, (fragment, text)
    assert str(project.root) not in text, text
    assert str(project.root).replace('\\', '/') not in text, text
    return text


def archive_mapping(archive):
    with zipfile.ZipFile(archive) as z:
        metadata = json.loads(z.read('module.json'))
        image = z.read('bytecode.bin')
        assert b'PRIVATE_SOURCE_COMMENT_NOT_FOR_SHIPPING' not in image
        return metadata, image


with tempfile.TemporaryDirectory(prefix='goat-source-diagnostics-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)

    # The same basename denotes an internal file and two distinct external files.
    # Registration follows include order; execution selects each existing identity.
    outputs = []
    for index, target in enumerate(['local', 'first', 'second']):
        p = suite.project('external-' + target)
        archive = p.compile('app',
                            'const local = include "math.goat";\n'
                            'const first = include "../../outside-a/math.goat";\n'
                            'const second = include "../../outside-b/math.goat";\n'
                            + target + '.fail();\n', files={
            'math.goat': 'public fail = func() {\n    throw "internal failure";\n};',
            '../../outside-a/math.goat':
                '// PRIVATE_SOURCE_COMMENT_NOT_FOR_SHIPPING\n'
                'public fail = func() {\n    throw "first failure";\n};',
            '../../outside-b/math.goat':
                '// PRIVATE_SOURCE_COMMENT_NOT_FOR_SHIPPING\n'
                'public fail = func() {\n    throw "second failure";\n};'})
        metadata, image = archive_mapping(archive)
        assert [(f['name'], f['external']) for f in metadata['files']] == [
            ('main.goat', False), ('math.goat', False), ('math.goat', True), ('math.goat', True)]
        assert str(p.root).encode() not in image
        assert str(p.root).encode('utf-32-le') not in image
        label = 'math.goat' if index == 0 else f'math.goat (external file #{index + 1})'
        text = diagnostic(p, archive, 'app@1:', label,
                          ':2:5' if index == 0 else ':3:5', 'fail')
        if index == 0:
            assert 'external file' not in text, text
        outputs.append(text)

    # Logical paths, source coordinates and names survive moving the whole project.
    p = suite.project('relocation')
    archive = p.compile('app', 'const lib = include "lib/vector.goat";\nlib.length();\n', files={
        'lib/vector.goat': 'public length = func() {\n    throw "mapping";\n};\n'})
    original_metadata, original_image = archive_mapping(archive)
    first = diagnostic(p, archive, 'app@1: lib/vector.goat:2:5', 'length')
    relocated = suite.project('relocated')
    shutil.copytree(p.root / 'sources', relocated.root / 'sources')
    moved_archive = relocated.root / 'app.gbin'
    relocated.invoke('compile', relocated.root / 'sources/app-1/module.gmod', moved_archive)
    moved_metadata, moved_image = archive_mapping(moved_archive)
    assert moved_metadata == original_metadata
    assert moved_image == original_image
    assert diagnostic(relocated, moved_archive) == first

    # Module context disambiguates identical file names, function names and ids.
    p = suite.project('imported')
    p.compile('library', 'external fail = func() {\n    throw "imported";\n};', version='2')
    archive = p.compile('app', 'const lib = import "library@2";\n'
                       'const fail = func() {\n    lib.fail();\n};\nfail();\n')
    text = diagnostic(p, archive, 'library@2: main.goat:2:5', 'app@1: main.goat:', 'fail')
    assert text.index('library@2:') < text.index('app@1:'), text

    # Returning an anonymous closure keeps the source function's own mapping.
    p = suite.project('anonymous')
    p.compile('library', 'external make = func() {\n'
              '    return func() {\n        throw "anonymous failure";\n    };\n};')
    archive = p.compile('app', 'const lib = import "library@1";\nconst f = lib.make();\nf();')
    text = diagnostic(p, archive, 'library@1: main.goat:3:9', 'app@1: main.goat:')
    assert re.search(r'anonymous|<function|function #', text, re.I), text

    # A failed initializer is not run again and its original trace is not replaced
    # by a later include site. The harness also compares formatted traces after GC.
    p = suite.project('saved-failure')
    archive = p.compile('app', 'try { include "bad.goat"; } catch(e) { println("caught"); }\n'
                        '\n\ninclude "bad.goat";\n', files={
                            'bad.goat': 'println("once");\nthrow "saved";\n'})
    text = diagnostic(p, archive, 'app@1: bad.goat:2:1', 'app@1: main.goat:1:',
                      expected='once\ncaught\n')
    assert 'main.goat:4:' not in text, text

    # Unicode labels are preserved rather than reduced to build-system encodings.
    p = suite.project('unicode')
    archive = p.compile('app', 'const lib = include "lib/вектор 😀.goat";\nlib.length();', files={
        'lib/вектор 😀.goat': 'public length = func() {\n    throw "unicode";\n};'})
    diagnostic(p, archive, 'app@1: lib/вектор 😀.goat:2:5', 'length')

    # A logical Unix filename may contain a literal backslash or newline. Escape
    # labels for single-line diagnostics; do not normalize the slash into a folder.
    if os.name != 'nt':
        p = suite.project('escaped-label')
        filename = 'literal\\name\nnext\x1b\x08\x7f\x85.goat'
        operand = json.dumps(filename, ensure_ascii=False).replace('\\u001b', '\x1b')
        archive = p.compile('app', 'const lib = include ' + operand + '; lib.fail();', files={
            filename: 'public fail = func() {\n    throw "escaped";\n};'})
        text = diagnostic(p, archive, 'literal\\\\name\\nnext\\u001b\\u0008\\u007f\\u0085.goat:2:5')
        assert 'literal/name' not in text and '\nnext' not in text, text
        assert not any(ord(c) < 32 and c != '\n' or 127 <= ord(c) <= 159 for c in text), text

    # The checked snapshot contains everything diagnostics need, including metadata
    # for dependencies: remove both archives and source trees before execution.
    p = suite.project('frozen')
    p.compile('library', 'const f = include "lib/error.goat"; external fail = f.fail;', files={
        'lib/error.goat': 'public fail = func() {\n    throw "frozen";\n};'})
    archive = p.compile('app', 'const lib = import "library@1";\nlib.fail();')
    process = subprocess.Popen([suite.exe, 'frozen', str(archive)], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               cwd=suite.unrelated, env=p.environment)
    try:
        assert process.stdout.readline().strip() == b'READY'
        shutil.rmtree(p.root)
        stdout, stderr = process.communicate(b'\n', timeout=60)
        assert process.returncode != 0 and not stdout, (process.returncode, stdout, stderr)
        text = stderr.decode('utf-8').replace('\r\n', '\n')
        assert 'library@1: lib/error.goat:2:5' in text, text
        assert 'app@1: main.goat:' in text and 'fail' in text, text
        assert str(p.root) not in text, text
        suite.count += 1
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

print(f'Module source diagnostic checks passed: {suite.count}')
