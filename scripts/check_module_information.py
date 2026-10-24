#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Information-only fields through the production reader, projection, hash and writer."""
import copy
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
WRITER = str(Path(sys.argv[2]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
M = json.loads((VECTORS / 'empty.module.json').read_text())
G = dict(formatVersion=1, name='empty', version='1', main='main.goat')
CODE = (VECTORS / 'empty.bytecode.bin').read_bytes()
FIELDS = ('description', 'license', 'homepage', 'authors', 'producers')
COUNT = 0


def run(value, mode='information-module', bad=False):
    global COUNT
    raw = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False).encode()
    if mode == 'hash':
        raw += b'\0' + CODE
    p = subprocess.run([EXE, '--' + mode], input=raw, capture_output=True, timeout=10)
    COUNT += 1
    assert p.returncode == (1 if bad else 0), (COUNT, mode, raw, p.stderr)
    if bad:
        assert not p.stdout
    return p.stdout


def info(value, kind='module', bad=False):
    result = run(value, 'information-' + kind, bad)
    return None if bad else json.loads(result)


def archive(metadata):
    global COUNT
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(metadata, ensure_ascii=False).encode())
        z.writestr('bytecode.bin', CODE)
    p = subprocess.run([WRITER, 'memory', 'unused'], input=stream.getvalue(),
                       capture_output=True, timeout=10)
    COUNT += 1
    assert p.returncode == 0, (COUNT, p.stderr)
    with zipfile.ZipFile(io.BytesIO(p.stdout)) as z:
        assert z.read('bytecode.bin') == CODE
        return json.loads(z.read('module.json')), p.stdout


# This standalone production-linked executable contains no network client entry
# points (including Windows import thunks). It links goat_module, not the
# executable VM. The projection itself uses only the existing in-memory parser.
symbols = subprocess.run(['nm', '-a', EXE], capture_output=True, text=True, check=True).stdout
for line in symbols.splitlines():
    symbol = line.split()[-1] if line.split() else ''
    symbol = re.sub(r'^_+(imp_+)?', '', symbol).split('@')[0]
    assert not re.match(r'^(socket|connect|getaddrinfo|gethostbyname|WSAStartup|WSASocket.*|'
                        r'WinHttp.*|Internet.*|curl_.*)$',
                        symbol), line
COUNT += 1

base_hash = run(M, 'hash')
golden = (VECTORS / 'empty.gbin').read_bytes()
for key in FIELDS:
    empty = [] if key in ('authors', 'producers') else ''
    normalized, blob = archive(dict(M, **{key: empty}))
    assert key not in normalized and blob == golden

for kind, base in [('gmod', G), ('module', M)]:
    assert info(base, kind) == {}
    keys = FIELDS if kind == 'module' else FIELDS[:-1]
    for key in keys:
        empty = [] if key in ('authors', 'producers') else ''
        assert info(dict(base, **{key: empty}), kind) == {}
        for invalid in [None, True, 1, {}, [] if isinstance(empty, str) else 'text']:
            info(dict(base, **{key: invalid}), kind, True)
    for text in [' ', '\t\r\n', 'Ёж 😀', 'e\u0301', 'é', '\0tail', 'A\0B',
                 'связаться через сайт', 'страница пока не создана',
                 'not a URL: no protocol', 'https://127.0.0.1:1/do-not-connect']:
        fields = dict(description=text, license=text, homepage=text,
                      authors=[dict(name=text, email=text)])
        expected = copy.deepcopy(fields)
        assert info(dict(base, **fields), kind) == expected
        # Escaping is decoded, never treated as author or producer identity.
        escaped = json.dumps(dict(base, **fields), ensure_ascii=True).encode()
        assert info(escaped, kind) == expected
        assert json.loads(run(dict(base, **fields), 'canonical-' + kind)) == dict(
            json.loads(run(base, 'canonical-' + kind)), **expected)
        if kind == 'module':
            assert run(dict(base, **fields), 'hash') == base_hash
            packed, blob = archive(dict(base, **fields))
            assert info(packed) == expected
            assert archive(packed)[1] == blob
    for key in ('name', 'email'):
        for value in [None, False, 4, [], {}]:
            author = dict(name='Ivan', email='x'); author[key] = value
            info(dict(base, authors=[author]), kind, True)
    for author in [{}, {'name': ''}, {'name': 'a', 'extra': 'x'}]:
        info(dict(base, authors=[author]), kind, True)
    authors = [dict(name='Иван'), dict(name='Иван', email='x'), dict(name='Анна', email='x'),
               dict(name='ivan'), dict(name='Иван '), dict(name='é'), dict(name='e\u0301')]
    assert info(dict(base, authors=authors), kind)['authors'] == authors
    assert info(dict(base, authors=list(reversed(authors))), kind)['authors'] == list(reversed(authors))
    assert info(dict(base, authors=[dict(name='x', email='')]), kind) == info(
        dict(base, authors=[dict(name='x')]), kind)
    for duplicate in [[dict(name='x'), dict(name='x')],
                      [dict(name='x'), dict(name='x', email='')],
                      [dict(name='x', email='y'), dict(name='x', email='y')]]:
        info(dict(base, authors=duplicate), kind, True)
    raw = json.dumps(base)[:-1] + ',"authors":[{"name":"x"},{"name":"\\u0078"}]}'
    info(raw.encode(), kind, True)
    for malformed in [b'\xc0\xaf', b'\xed\xa0\x80', b'\\ud800']:
        info((json.dumps(base)[:-1] + ',"description":"').encode() + malformed + b'"}', kind, True)
    # Projection never hides malformed executable fields or unknown properties.
    info(dict(base, version=None), kind, True)
    info(dict(base, unknown='ignored?'), kind, True)

for scalar in [None, False, 1, 'name', []]:
    info(dict(M, authors=[scalar]), bad=True)
    info(dict(M, producers=[scalar]), bad=True)
producer = dict(name='Goat Compiler', version='development build', buildId='stable compiler build')
for key in producer:
    for bad in [None, '', False, 1, [], {}]:
        info(dict(M, producers=[dict(producer, **{key: bad})]), bad=True)
    omitted = dict(producer); del omitted[key]
    info(dict(M, producers=[omitted]), bad=True)
info(dict(M, producers=[dict(producer, timestamp='now')]), bad=True)
info(dict(M, producers=[dict(producer, machine='/tmp/build')]), bad=True)
info(dict(M, producers=[producer, producer]), bad=True)
info(dict(G, producers=[producer]), 'gmod', True)
raw = json.dumps(dict(M, producers=[producer, producer])).replace('Goat Compiler', '\\u0047oat Compiler', 1)
info(raw.encode(), bad=True)
producers = [producer, dict(producer, version='2'), dict(producer, version='10'),
             dict(producer, buildId='another'), dict(producer, name='😀'),
             dict(producer, name='e\u0301'), dict(producer, name='é'),
             dict(name=' ', version='\0version', buildId='🙂\0id')]
ordered = sorted(producers, key=lambda p: tuple(p[k].encode() for k in ('name', 'version', 'buildId')))
for values in (producers, list(reversed(producers))):
    expected = dict(producers=ordered)
    assert info(dict(M, producers=values)) == expected
    assert run(dict(M, producers=values), 'hash') == base_hash
    packed, blob = archive(dict(M, producers=values))
    assert info(packed) == expected
    assert archive(packed)[1] == blob  # no regenerated buildId/provenance
# Each individual field remains informational, even when its exact value changes.
for key, value in [('description', 'different'), ('license', 'custom license, not SPDX'),
                   ('homepage', 'not a URL'), ('authors', [dict(name='Z'), dict(name='A')]),
                   ('producers', producers)]:
    assert run(dict(M, **{key: value}), 'hash') == base_hash
    assert info(dict(M, **{key: value})) != {}
# None of the significant text distinctions are normalized away.
for a, b in [('Name', 'name'), ('a', ' a'), ('é', 'e\u0301'), ('x', 'x\0'), ('', ' ')]:
    assert info(dict(M, description=a)) != info(dict(M, description=b))
    assert info(dict(M, authors=[dict(name='x', email=a)])) != info(
        dict(M, authors=[dict(name='x', email=b)]))
    if a:
        assert info(dict(M, producers=[dict(producer, name=a)])) != info(
            dict(M, producers=[dict(producer, name=b)]))
print(f'Module information checks passed: {COUNT}')
