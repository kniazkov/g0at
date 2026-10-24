#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Ordered, fixed program-wide directory search; never skip a bad first candidate."""
import copy
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
OK, ARGUMENT, IO, NOT_FOUND, INVALID, IDENTITY, HASH = range(7)
COUNT = 0


def image():
    ins = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
           (7, 0, 0, 0), (50, 0, 0, 0)]
    of = 64 + len(ins) * 8
    on = of + 16
    ol = on + 32
    end = ol + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(ins), 0, 1, 1, 2,
                        64, of, of, on, ol, end)
            + b''.join(struct.pack('<BBHI', *v) for v in ins)
            + struct.pack('<4I', 0, len(ins), 0, 0)
            + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
            + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
            + struct.pack('<6I', 4, 2, 0, 2, 1, 1))


IMAGE = image()


def archive(name='sample', version='1', file='main.goat'):
    m = dict(copy.deepcopy(BASE), name=name, version=version,
             functions=[dict(functionId=1, file=0, instruction=1, parameters=[])])
    m['files'][0]['name'] = file
    m['mainHash'] = ref.main_hash(m, IMAGE)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(m).encode())
        z.writestr('bytecode.bin', IMAGE)
    return stream.getvalue(), m['mainHash']


def put(path, data=b''):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def norm(path):
    return os.path.normcase(os.path.normpath(str(path)))


def paths(actual, expected):
    assert list(map(norm, actual)) == list(map(norm, expected)), (actual, expected)


def run(root, module_dir='', environment='', name='sample', version='1', expected_hash='',
        cwd=None, after_cwd='', mutation=None, status=OK, preparation=OK):
    global COUNT
    COUNT += 1
    args = [EXE, str(root), str(module_dir), environment, name, version,
            expected_hash, str(after_cwd)]
    p = subprocess.Popen(args, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, encoding='utf-8')
    prepared = json.loads(p.stdout.readline())
    assert prepared['status'] == preparation, (COUNT, prepared, preparation)
    if mutation:
        mutation()
    output, errors = p.communicate('\n', timeout=20)
    assert p.returncode == 0, (COUNT, p.returncode, output, errors)
    result = json.loads(output) if output else None
    if preparation == OK:
        assert result['status'] == status, (COUNT, result, status)
    return prepared, result


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-search-') as temp:
    root = Path(temp)
    project, launch, elsewhere = [root / n for n in ('проект', 'launch', 'cwd')]
    for d in (project, launch, elsewhere):
        d.mkdir()
    source = put(project / 'app.goat')
    local = project / 'modules'
    explicit, env1, env2 = [root / n for n in ('output', 'env1', 'env2')]
    directories = [local, explicit, env1, env2]
    good, digest = archive()
    other, _ = archive(file='other.goat')
    candidates = [d / 'sample/1.gbin' for d in directories]
    for path in candidates:
        put(path, good)
    environment = os.pathsep.join(map(str, [env1, env2]))
    # Precedence is identical for descriptor, source and compiled program roots.
    for suffix in ('gmod', 'goat', 'gbin'):
        entry = put(project / ('app.' + suffix))
        prepared, result = run(entry, explicit, environment)
        paths(prepared['directories'], directories)
        paths(result['attempted'], candidates[:1])
        paths([result['archive']], candidates[:1])
        assert result['hash'] == digest
    # No probing of later paths after a hit. Fall through only definitive absence.
    for index in range(4):
        _, result = run(source, explicit, environment)
        paths(result['attempted'], candidates[:index + 1])
        paths([result['archive']], candidates[index:index + 1])
        candidates[index].unlink()
    _, result = run(source, explicit, environment, status=NOT_FOUND)
    paths(result['attempted'], candidates)
    # No recursion, version guessing, or discovery from a library's directory.
    put(local / 'nested/sample/1.gbin', good)
    put(local / 'sample/2.gbin', good)
    put(env2 / 'modules/sample/1.gbin', good)
    run(source, explicit, environment, status=NOT_FOUND)
    put(candidates[1], good)
    for bad, status in [(b'broken zip', INVALID), (archive(name='wrong')[0], IDENTITY),
                        (archive(version='2')[0], IDENTITY), (other, HASH)]:
        put(candidates[0], bad)
        _, result = run(source, explicit, environment, expected_hash=digest, status=status)
        paths(result['attempted'], candidates[:1])
        assert result['archive'] is None
    put(candidates[0], good)
    run(source, explicit, environment, expected_hash=digest)
    run(source, explicit, environment, expected_hash='bad', status=ARGUMENT)
    run(source, explicit, environment, name='../sample', status=ARGUMENT)
    run(source, explicit, environment, version='../1', status=ARGUMENT)
    candidates[0].unlink()
    candidates[0].mkdir()
    _, result = run(source, explicit, environment, status=IO)
    paths(result['attempted'], candidates[:1])
    candidates[0].rmdir()
    if os.name != 'nt':
        os.mkfifo(candidates[0])
        run(source, explicit, environment, status=IO)
        candidates[0].unlink()
    # Empty path segments ignored; aliases dedup without changing order.
    prepared, _ = run(source, local, os.pathsep.join(['', str(local), str(explicit),
                                                    str(explicit / '.'), '', str(env1), '']))
    paths(prepared['directories'], [local, explicit, env1])
    # Relative CLI/environment paths resolve from initial cwd, never the root directory.
    relative = elsewhere / 'relative'
    put(relative / 'sample/1.gbin', good)
    prepared, result = run(source, './relative', '.relative-missing' + os.pathsep + './relative',
                           cwd=elsewhere, after_cwd=root)
    paths(prepared['directories'], [local, relative, elsewhere / '.relative-missing'])
    paths([result['archive']], [relative / 'sample/1.gbin'])
    # Separator is native. The other platform separator is literal on POSIX.
    if os.name != 'nt':
        literal = root / 'one;two'
        put(literal / 'sample/1.gbin', good)
        run(source, environment=str(literal))
    # Paths are literal; no shell expansion of home or variable syntax.
    for literal_name in ('~', '$GOAT_MODULE_PATH', '%GOAT_MODULE_PATH%'):
        literal = elsewhere / literal_name
        put(literal / 'sample/1.gbin', good)
        prepared, result = run(source, environment=literal_name, cwd=elsewhere)
        paths([result['archive']], [literal / 'sample/1.gbin'])
    prepared, result = run(source, environment=os.pathsep * 3, status=NOT_FOUND)
    paths(prepared['directories'], [local])
    if os.name != 'nt':
        # Do not simplify missing/..: resolution must preserve filesystem semantics.
        prepared, _ = run(source, root / 'missing/a/../b', str(root / 'missing/b'), status=NOT_FOUND)
        paths(prepared['directories'], [local, root / 'missing/a/../b', root / 'missing/b'])
        # Lexical normalization of file/.. would invent a candidate that does not exist.
        put(root / 'file')
        put(root / 'vendor/sample/1.gbin', good)
        run(source, root / 'file/../vendor', status=IO)
        run(source, root / 'nonexistent/../vendor', status=NOT_FOUND)
    else:
        # Reject incomplete Windows roots rather than consult per-drive cwd.
        for incomplete in ('C:relative', '/relative', '\\relative'):
            run(source, incomplete, preparation=IO)
    prepared, _ = run(source, str(source.parent / './modules'),
                      str(source.parent) + '/./modules//', status=NOT_FOUND)
    paths(prepared['directories'], [local])
    # Unused missing directories are legal; no preflight dependency discovery.
    prepared, result = run(source, root / 'missing', str(root / 'also-missing'), status=NOT_FOUND)
    assert len(prepared['directories']) == 3
    run(root / 'absent.goat', preparation=IO)
    try:
        alias = launch / 'app.goat'
        alias.symlink_to(source)
    except OSError as error:
        if os.name != 'nt':
            raise
        print('Windows symlink fixture unavailable:', error)
    else:
        # Distractor beside launcher must not supersede real-root modules.
        put(launch / 'modules/sample/1.gbin', b'not the real project')
        put(candidates[0], good)
        for suffix in ('gmod', 'gbin'):
            link = launch / ('app.' + suffix)
            link.symlink_to(project / ('app.' + suffix))
            prepared, result = run(link, explicit, environment)
            paths([prepared['root']], [project / ('app.' + suffix)])
            paths([result['archive']], candidates[:1])
        replacement = put(elsewhere / 'replacement.goat')
        put(elsewhere / 'modules/sample/1.gbin', b'wrong project')
        def retarget():
            alias.unlink()
            alias.symlink_to(replacement)
        prepared, result = run(alias, explicit, environment, mutation=retarget, after_cwd=elsewhere)
        paths([prepared['root']], [source])
        paths([result['archive']], candidates[:1])
        directory_alias = root / 'modules-alias'
        directory_alias.symlink_to(local, target_is_directory=True)
        prepared, _ = run(source, directory_alias, str(local))
        paths(prepared['directories'], [local])
        candidates[0].unlink()
        candidates[0].symlink_to(root / 'no-such-module')
        _, result = run(source, explicit, environment, status=IO)
        paths(result['attempted'], candidates[:1])
        candidates[0].unlink()
    # A later unavailable directory is irrelevant when the local candidate succeeds.
    put(candidates[0], good)
    denied = root / 'denied'
    (denied / 'child').mkdir(parents=True)
    if os.name != 'nt':
        denied.chmod(0)
        try:
            _, result = run(source, denied / 'child')
            paths(result['attempted'], candidates[:1])
        finally:
            denied.chmod(0o700)
    # Keep access failures distinct from absence when ACLs can actually be changed.
    if os.name != 'nt' and os.geteuid() != 0:
        put(candidates[0], good)
        candidates[0].chmod(0)
        try:
            with candidates[0].open('rb'):
                pass
        except PermissionError:
            _, result = run(source, explicit, environment, status=IO)
            paths(result['attempted'], candidates[:1])
        else:
            assert not os.environ.get('CI'), 'CI must enforce permission-denied search fixture'
            print('Access-denied fixture unavailable: filesystem ignores mode bits')
        finally:
            candidates[0].chmod(0o600)
print(f'Module directory search: {COUNT} fixtures passed')
