#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""End-to-end nonexecuting merge, metadata policy, paths and atomic publication."""
import copy
import hashlib
import io
import itertools
import json
import os
from pathlib import Path
import select
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
CC = shutil.which(next((x for x in sys.argv[2:] if not x.startswith('--')),
                      os.environ.get('CC', 'gcc')))
ALLOW_UNSUPPORTED_ACL = '--allow-unsupported-acl' in sys.argv[2:]
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
CODE = (VECTORS / 'empty.bytecode.bin').read_bytes()
TARGET = json.loads((VECTORS / 'target.json').read_text())
GOLDEN = (VECTORS / 'empty.gbin').read_bytes()
COUNT = 0


def pack(doc=None, code=CODE, payloads=None, rehash=True, reverse=False):
    doc = copy.deepcopy(BASE if doc is None else doc)
    if rehash:
        doc['mainHash'] = ref.main_hash(doc, code)
    parts = [('module.json', json.dumps(doc, ensure_ascii=False).encode()),
             ('bytecode.bin', code)] + list((payloads or {}).items())
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_STORED) as z:
        for name, data in reversed(parts) if reverse else parts:
            z.writestr(name, data)
    return out.getvalue()


def put(path, data):
    path.write_bytes(data)
    return path


def run(*args, status=0, cwd=None, env=None, input=b''):
    global COUNT
    COUNT += 1
    p = subprocess.run([EXE, *map(str, args)], input=input, capture_output=True,
                       cwd=cwd, env=dict(os.environ, **(env or {})), timeout=15)
    expected = status if isinstance(status, tuple) else (status,)
    assert p.returncode in expected, (COUNT, args, p.returncode, expected, p.stderr)
    if p.returncode:
        assert p.stderr and not p.stdout, (COUNT, p)
    return p


def metadata(path):
    with zipfile.ZipFile(path) as z:
        return json.loads(z.read('module.json'))


def success(out, *sources, **kwargs):
    p = run(out, *sources, **kwargs)
    doc = metadata(out)
    lines = p.stdout.decode('utf-8').splitlines()
    assert len(lines) == 1, p.stdout
    # Paths are quoted, including escaped Windows separators. Compare decoded
    # values rather than searching for raw filesystem strings in display text.
    decoder = json.JSONDecoder()
    path, offset = decoder.raw_decode(lines[0])
    assert path == str(out), (path, out)
    assert lines[0][offset:offset + 2] == ': ', lines
    name, offset = decoder.raw_decode(lines[0], offset + 2)
    assert name == doc['name'] and lines[0][offset] == '@', lines
    version, offset = decoder.raw_decode(lines[0], offset + 1)
    assert version == doc['version'], lines
    assert lines[0].endswith(f"; {len(doc.get('native', []))} native variants"), lines
    with zipfile.ZipFile(out) as z:
        assert z.read('module.json') == ref.canonical(doc)
        assert doc == ref.normalize(doc)
        assert doc['mainHash'] == ref.main_hash(doc, z.read('bytecode.bin'))
        assert z.namelist() == ['module.json', 'bytecode.bin'] + sorted(
            n['library'] for n in doc.get('native', []))
        for entry in z.infolist():
            assert entry.compress_type == zipfile.ZIP_DEFLATED
            assert entry.date_time == (1980, 1, 1, 0, 0, 0)
        for native in doc.get('native', []):
            assert hashlib.sha256(z.read(native['library'])).hexdigest() == native['hash']
    return doc


def fail_preserving(out, *sources):
    before = out.read_bytes()
    siblings = set(out.parent.iterdir())
    p = run(out, *sources, status=1)
    assert out.read_bytes() == before and set(out.parent.iterdir()) == siblings
    assert b'Choose author order' not in p.stderr
    return p


def function_image():
    instructions = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
                    (7, 0, 0, 0), (50, 0, 0, 0)]
    of = 64 + len(instructions) * 8
    on, ol = of + 16, of + 48
    end = ol + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(instructions), 0, 1, 1, 2,
                        64, of, of, on, ol, end)
            + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + struct.pack('<4I', 0, len(instructions), 0, 0)
            + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
            + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
            + struct.pack('<6I', 4, 2, 0, 2, 1, 1))


IMAGE = function_image()
NATIVE_BASE = dict(copy.deepcopy(BASE), functions=[dict(functionId=1, file=0,
                                                        instruction=1, parameters=[])])
PAYLOAD = b'not an executable library: merging must not load this'


def native(target=None, library='native/input.so', payload=PAYLOAD, compiler=None):
    item = dict(library=library, hash=hashlib.sha256(payload).hexdigest(),
                goatAbiVersion=1, target=copy.deepcopy(target or TARGET),
                specializations=[dict(functionId=1, specializationId=1, symbol='goat_fn_1_v1',
                    signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                    when=dict(argumentTypes=[]))])
    if compiler is not None:
        item['cCompiler'] = compiler
    return item


def native_archive(path, variants, payloads=None):
    doc = dict(copy.deepcopy(NATIVE_BASE), native=variants)
    doc['mainHash'] = ref.main_hash(NATIVE_BASE, IMAGE)
    return put(path, pack(doc, IMAGE,
                         payloads or {v['library']: PAYLOAD for v in variants}, rehash=False))


with tempfile.TemporaryDirectory(prefix='goat-merge-') as directory:
    root = Path(directory)
    src = put(root / 'source.gbin', pack())
    second = put(root / 'second.gbin', pack(reverse=True))
    out = root / 'result'
    for args in ((), (out,), ('--unknown',), ('--output', out, src), ('--force', out, src)):
        run(*args, status=2)
        assert not out.exists()
    for flag in ('--help', '--version'):
        assert run(flag).stdout
        assert not out.exists()
    success(out, src)
    assert out.read_bytes() == GOLDEN and src.read_bytes() != GOLDEN
    out.unlink()
    success(out, src, second, src, second)
    assert out.read_bytes() == GOLDEN
    # Exact output path, Unicode/non-BMP arguments, literal '-' and explicit option separator.
    for name in ('result.gbin', 'result.other', 'результат 😀 с пробелами'):
        target = root / name
        success(target, src)
        assert target.read_bytes() == GOLDEN and not Path(str(target) + '.gbin').exists()
    if os.name != 'nt':
        # Exercise display escaping even when CI is not running on Windows.
        success(root / 'literal\\backslash', src)
    dash_dir = root / 'dash-dir'; dash_dir.mkdir()
    run('--', '-', str(src), cwd=dash_dir)
    assert (dash_dir / '-').read_bytes() == GOLDEN
    literal = put(root / '-', pack())
    p = run('--', 'literal-output', '-', cwd=root)
    assert (root / 'literal-output').read_bytes() == GOLDEN and not p.stdout.startswith(b'PK')
    source_unicode = put(root / 'источник 😀.other', pack())
    success(root / 'unicode-output', source_unicode)
    for name in ('input', 'input.other'):
        raw = put(root / name, pack())
        put(Path(str(raw) + '.gbin'), b'not selected')
        success(root / (name + '-result'), raw)
    # Never try source+'.gbin' as a fallback.
    put(root / 'missing.gbin', pack())
    fail_preserving(out, root / 'missing')

    # Full validation and fail-fast source order. A later nonexistent source must not appear.
    corrupted = put(root / 'damaged', b'not a zip')
    later = root / 'must-not-open-after-damage'
    for sources in ((corrupted, later), (src, corrupted, later)):
        p = fail_preserving(out, *sources)
        assert json.dumps(str(corrupted), ensure_ascii=False).encode() in p.stderr and json.dumps(str(later), ensure_ascii=False).encode() not in p.stderr
    bad_docs = []
    doc = copy.deepcopy(BASE); doc['mainHash'] = '0' * 64
    bad_docs.append(pack(doc, rehash=False))
    for key, value in [('dependencies', None), ('authors', None), ('authors', [{'name': ''}]),
                       ('authors', [{'name': 'A'}, {'name': 'A', 'email': ''}]),
                       ('description', None), ('homepage', None), ('license', None),
                       ('authors', [{'name': 'A', 'email': None}]), ('unknown', True)]:
        doc = dict(copy.deepcopy(BASE), **{key: value})
        # Hash projection does not need to accept deliberately malformed fields.
        bad_docs.append(pack(doc, rehash=False))
    bad_code = bytearray(CODE); bad_code[64] = 255
    bad_docs.append(pack(code=bad_code))
    for data in bad_docs:
        put(corrupted, data)
        fail_preserving(out, src, corrupted)
    different = put(root / 'different', pack(dict(copy.deepcopy(BASE), version='different')))
    fail_preserving(out, src, different)

    # Only explicit sources matter, including when destination contains another module/garbage.
    replacement = run(out, src, status=(0, 1) if ALLOW_UNSUPPORTED_ACL else 0)
    supported = replacement.returncode == 0
    if not supported:
        assert ALLOW_UNSUPPORTED_ACL and out.read_bytes() == GOLDEN
        assert b'filesystem 5' in replacement.stderr, replacement.stderr
        print('Explicit local-only skip: filesystem does not support required ACL transfer')
    if supported:
        for old in (b'old corrupt archive', different.read_bytes(), second.read_bytes()):
            put(out, old)
            success(out, src)
            assert out.read_bytes() == GOLDEN
        put(out, b'explicit damaged source')
        fail_preserving(out, out, src)
        put(out, pack())
        success(out, out, src)
        assert out.read_bytes() == GOLDEN
        if os.name != 'nt':
            out.chmod(0o640); src.chmod(0o600)
            success(out, src)
            assert stat.S_IMODE(out.stat().st_mode) == 0o640
            assert stat.S_IMODE(src.stat().st_mode) == 0o600
        old_link = root / 'old-hard-link'
        put(out, b'old hard-linked content'); os.link(out, old_link)
        success(out, src)
        assert old_link.read_bytes() == b'old hard-linked content'
    else:
        put(out, GOLDEN)
    if os.name != 'nt':
        old_umask = os.umask(0o027)
        try:
            created = root / 'new-umask'
            success(created, src)
            assert stat.S_IMODE(created.stat().st_mode) == 0o640
        finally:
            os.umask(old_umask)

    # Repacking never executes even valid looping bytecode or resolves dependency files.
    looping = bytearray(CODE)
    looping[64] = 3  # JUMP 0: valid control flow, would never finish if executed.
    loop_source = put(root / 'loop-source', pack(code=looping))
    success(root / 'loop-result', loop_source)
    dependencies = [dict(name='unavailable', version='1', hash='1' * 64),
                    dict(name='unavailable', version='2', hash='2' * 64)]
    dep_a = put(root / 'dependency-a', pack(dict(copy.deepcopy(BASE), dependencies=dependencies)))
    dep_b = put(root / 'dependency-b', pack(dict(copy.deepcopy(BASE), dependencies=dependencies[::-1])))
    dep_result = root / 'dependency-result'
    assert success(dep_result, dep_a, dep_b)['dependencies'] == dependencies
    bad_dependency = copy.deepcopy(dependencies); bad_dependency[0]['hash'] = '3' * 64
    put(dep_b, pack(dict(copy.deepcopy(BASE), dependencies=bad_dependency)))
    fail_preserving(out, dep_a, dep_b)

    # Information normalization, exact decoded text, producers, and sole-author order.
    for key in ('description', 'license', 'homepage', 'authors', 'dependencies'):
        empty = [] if key in ('authors', 'dependencies') else ''
        equivalent = put(root / 'equivalent', pack(dict(copy.deepcopy(BASE), **{key: empty})))
        for sources in ((src, equivalent), (equivalent, src)):
            target = root / ('normalized-' + str(COUNT))
            doc = success(target, *sources)
            assert key not in doc and target.read_bytes() == GOLDEN
    for key in ('description', 'license', 'homepage'):
        for value in (' ', 'é', 'e\u0301', 'Ёж 😀', 'a\0b'):
            item = dict(copy.deepcopy(BASE), **{key: value})
            a = put(root / 'info-a', pack(item)); b = put(root / 'info-b', pack(item))
            target = root / ('text-' + str(COUNT))
            assert success(target, a, b)[key] == value
            fail_preserving(out, a, src)
            fail_preserving(out, src, a)
    authors = [dict(name='Zed'), dict(name='Анна', email=''), dict(name='Zed', email='other')]
    a = put(root / 'authors-a', pack(dict(copy.deepcopy(BASE), authors=authors)))
    empty = put(root / 'authors-empty', pack(dict(copy.deepcopy(BASE), authors=[])))
    target = root / 'sole-authors'
    expected_authors = [dict(name='Zed'), dict(name='Анна'), dict(name='Zed', email='other')]
    assert success(target, empty, a, a, empty)['authors'] == expected_authors
    b = put(root / 'authors-b', a.read_bytes())
    for sources in ((a, b), (a, empty, b), (b, a)):
        fail_preserving(out, *sources)
    # Aliases are distinct argument strings, not deduplicated by bytes, inode or canonical path.
    fail_preserving(out, str(a), str(a.parent) + '/./' + a.name)
    producers = [dict(name='Z', version='2', buildId='second'),
                 dict(name='A', version='1', buildId='first')]
    pa = put(root / 'producer-a', pack(dict(copy.deepcopy(BASE), producers=producers[:1])))
    pb = put(root / 'producer-b', pack(dict(copy.deepcopy(BASE), producers=producers[::-1])))
    for sources in ((pa, pb), (pb, pa)):
        target = root / ('producers-' + str(COUNT))
        assert success(target, *sources)['producers'] == producers[::-1]

    # Source type checks and invalid destinations happen before any authors prompt/temp file.
    file_parent = put(root / 'file-parent', b'not a directory')
    for destination in (root, root / 'missing-parent' / 'result',
                        root / 'missing' / 'nested' / 'result', file_parent / 'result'):
        before = set(root.iterdir())
        p = run(destination, a, b, status=1)
        assert b'Choose author order' not in p.stderr and set(root.iterdir()) == before
        assert json.dumps(str(destination), ensure_ascii=False).encode() in p.stderr, p.stderr
    fail_preserving(out, root)
    target_bytes = pack()
    target = put(root / 'link-target', target_bytes)
    link = root / 'symlink'
    try:
        link.symlink_to(target)
    except OSError as error:
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1314:
            raise
    else:
        success(root / 'linked-source', link)
        for sources in ((a, b), (link, src)):
            p = run(link, *sources, status=1)
            assert link.is_symlink() and target.read_bytes() == target_bytes
            assert b'Choose author order' not in p.stderr
        target.unlink()
        run(link, src, status=1)
        assert link.is_symlink() and not target.exists()
        link.unlink()
        parent_link = root / 'parent-link'; parent_link.symlink_to(root, target_is_directory=True)
        success(parent_link / 'parent-linked-result', src)
    if os.name != 'nt':
        fifo = root / 'fifo'; os.mkfifo(fifo)
        fail_preserving(out, fifo)
        run(fifo, a, b, status=1)
        assert stat.S_ISFIFO(fifo.stat().st_mode)
        import socket
        try:
            sock = socket.socket(socket.AF_UNIX)
        except PermissionError:
            if '--allow-restricted-sockets' not in sys.argv[2:]:
                raise
            print('Explicit local-only skip: sandbox prohibits Unix socket fixtures')
        else:
            with sock:
                socket_path = root / 'socket'; sock.bind(str(socket_path))
                fail_preserving(out, socket_path)
                run(socket_path, src, status=1)
                assert stat.S_ISSOCK(socket_path.stat().st_mode)
        fail_preserving(out, Path('/dev/null'))

    # Native payloads, requirements, deduplication and compiler provenance use all inputs.
    linux = native()
    windows_target = copy.deepcopy(TARGET)
    windows_target.update(os='windows', minimumOs=[10],
                          runtime=dict(family='ucrt', minimumVersion=[0]))
    windows = native(windows_target, 'native/win.dll')
    na = native_archive(root / 'native-a', [linux])
    nb = native_archive(root / 'native-b', [windows])
    merged = []
    for sources in ((na, nb), (nb, na), (na, nb, na)):
        target = root / ('native-result-' + str(COUNT))
        assert len(success(target, *sources)['native']) == 2
        merged.append(target.read_bytes())
    assert len(set(merged)) == 1
    combined = native_archive(root / 'native-combined', [windows, linux])
    target = root / 'combined-result'
    assert len(success(target, combined)['native']) == 2 and target.read_bytes() == merged[0]
    alias = copy.deepcopy(linux); alias['library'] = 'native/alias.so'
    nc = native_archive(root / 'native-c', [alias])
    assert len(success(root / 'native-dedup', na, nc)['native']) == 1
    different_payload = b'a different native library'
    conflict = native(payload=different_payload)
    nc = native_archive(nc, [conflict], {conflict['library']: different_payload})
    fail_preserving(out, na, nc)
    overlap = copy.deepcopy(linux); overlap['target']['minimumOs'] = [6]
    native_archive(nc, [overlap])
    fail_preserving(out, na, nc)
    # Requirements intersect on a hypothetical host, not only this CI machine.
    for changes in ({'cpuFeatures': ['avx2']},
                    {'runtime': dict(family='glibc', minimumVersion=[9])}):
        overlap = copy.deepcopy(linux); overlap['target'].update(changes)
        native_archive(nc, [overlap])
        fail_preserving(out, na, nc)
    # Incompatible categorical runtime/architecture variants are retained together.
    for changes in ({'runtime': dict(family='musl', minimumVersion=[1])},
                    {'architecture': 'aarch64', 'cpuFeatures': []}):
        distinct = copy.deepcopy(linux); distinct['target'].update(changes)
        native_archive(nc, [distinct])
        assert len(success(root / ('category-' + str(COUNT)), na, nc)['native']) == 2
    # Same library bytes cannot resolve disagreeing specialization declarations.
    conflicting_spec = copy.deepcopy(linux)
    conflicting_spec['specializations'][0]['symbol'] = 'another_adapter'
    native_archive(nc, [conflicting_spec])
    fail_preserving(out, na, nc)
    unknown_abi = copy.deepcopy(linux); unknown_abi['goatAbiVersion'] = 999
    native_archive(nc, [unknown_abi])
    assert len(success(root / 'unknown-abi-result', na, nc)['native']) == 2
    same_unknown = copy.deepcopy(unknown_abi); same_unknown['library'] = 'native/unknown.so'
    nd = native_archive(root / 'native-d', [same_unknown])
    assert len(success(root / 'unknown-abi-dedup', nc, nd)['native']) == 1
    unknown_distinct = copy.deepcopy(unknown_abi)
    unknown_distinct['target']['minimumOs'] = [6]
    native_archive(nd, [unknown_distinct])
    assert len(success(root / 'unknown-abi-requirements', nc, nd)['native']) == 2
    same_unknown['hash'] = hashlib.sha256(different_payload).hexdigest()
    native_archive(nd, [same_unknown], {same_unknown['library']: different_payload})
    fail_preserving(out, nc, nd)
    compiler = dict(name='C compiler', version='17 diagnostic')
    described = copy.deepcopy(linux); described['cCompiler'] = compiler
    native_archive(nc, [described])
    for sources in ((na, nc), (nc, na), (nc, nc)):
        target = root / ('compiler-' + str(COUNT))
        assert success(target, *sources)['native'][0]['cCompiler'] == compiler
    nd = root / 'native-d'
    for field in ('name', 'version'):
        wrong = copy.deepcopy(described); wrong['cCompiler'][field] += ' different'
        native_archive(nd, [wrong])
        for sources in itertools.permutations((na, nc, nd)):
            fail_preserving(out, *sources)
    bad = copy.deepcopy(linux); bad['hash'] = '0' * 64
    native_archive(nd, [bad])
    fail_preserving(out, na, nd)
    bad = copy.deepcopy(linux); bad['specializations'][0]['functionId'] = 999
    native_archive(nd, [bad])
    fail_preserving(out, na, nd)
    for compiler_bad in (None, {}, {'name': 'C'}, {'name': '', 'version': '1'},
                         {'name': 'C', 'version': '1', 'unknown': 'x'}):
        bad = copy.deepcopy(linux); bad['cCompiler'] = compiler_bad
        native_archive(nd, [bad])
        fail_preserving(out, nd)

    # An actual DLL/SO constructor must never run, even for a same-platform payload.
    assert CC, 'A real C compiler is required for the no-load integration check'
    c = put(root / 'constructor.c', br'''#include <stdio.h>
#include <stdlib.h>
static void mark(void) {
    const char *p = getenv("GOAT_MERGE_MARKER");
    if (p) { FILE *f = fopen(p, "wb"); if (f) { fputs("loaded", f); fclose(f); } }
}
#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h; (void)reserved; if (reason == DLL_PROCESS_ATTACH) mark(); return TRUE;
}
#else
__attribute__((constructor)) static void initialize(void) { mark(); }
#endif
''')
    library = root / ('constructor.dll' if os.name == 'nt' else 'constructor.so')
    command = [CC, '-shared', str(c), '-o', str(library)]
    if os.name != 'nt':
        command.insert(1, '-fPIC')
    built = subprocess.run(command, capture_output=True, timeout=60)
    assert built.returncode == 0, (command, built.stderr)
    payload = library.read_bytes()
    v = native(windows_target if os.name == 'nt' else TARGET,
               'native/constructor' + library.suffix, payload)
    native_archive(nd, [v], {v['library']: payload})
    marker = root / 'constructor-marker'
    success(root / 'constructor-result', nd, env={'GOAT_MERGE_MARKER': str(marker)})
    assert not marker.exists()

    # Real terminal input, retries, EOF, and destination preservation while choice is pending.
    if os.name != 'nt':
        import pty
        import termios

        def interactive(destination, sources, response, expected=0, during=None):
            global COUNT
            COUNT += 1
            master, slave = pty.openpty()
            attributes = termios.tcgetattr(slave)
            attributes[3] &= ~termios.ECHO
            termios.tcsetattr(slave, termios.TCSANOW, attributes)
            before = destination.read_bytes() if destination.exists() else None
            p = subprocess.Popen([EXE, str(destination), *map(str, sources)],
                                 stdin=slave, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(slave)
            diagnostic = b''
            deadline = time.monotonic() + 10
            try:
                while b'Choose author order' not in diagnostic:
                    assert time.monotonic() < deadline, diagnostic
                    ready, _, _ = select.select([p.stderr], [], [], 0.2)
                    if ready:
                        chunk = os.read(p.stderr.fileno(), 65536)
                        assert chunk, (p.poll(), diagnostic)
                        diagnostic += chunk
                assert (destination.read_bytes() if destination.exists() else None) == before
                if during:
                    during()
                os.write(master, response)
                stdout, tail = p.communicate(timeout=10)
                diagnostic += tail
                assert p.returncode == expected, (p.returncode, stdout, diagnostic)
                if expected:
                    assert not stdout
                    assert (destination.read_bytes() if destination.exists() else None) == before
                else:
                    assert len(stdout.splitlines()) == 1
                return diagnostic
            finally:
                if p.poll() is None:
                    p.kill(); p.communicate()
                os.close(master)

        author_a = [dict(name='First'), dict(name='Second', email='s@example.invalid')]
        author_b = [dict(name='Second', email='s@example.invalid'), dict(name='Third')]
        put(a, pack(dict(copy.deepcopy(BASE), authors=author_a)))
        put(b, pack(dict(copy.deepcopy(BASE), authors=author_b)))
        terminal_out = root / 'terminal-result'
        diagnostic = interactive(terminal_out, (a, empty, b), b'1 1\n0 2 3\n3 1 2\n')
        assert metadata(terminal_out)['authors'] == [author_b[1], *author_a]
        assert diagnostic.count(b'Choose author order') == 3
        interactive(root / 'terminal-eof', (a, b), b'\x04', expected=1)
        if supported:
            put(terminal_out, b'old destination while choosing')
            interactive(terminal_out, (a, b), b'\x04', expected=1)
            interactive(terminal_out, (a, b), b'2 3 1\n')
            assert metadata(terminal_out)['authors'] == [author_a[1], author_b[1], author_a[0]]
        # Source bytes are retained before the prompt, not reopened after author choice.
        frozen_a = a.read_bytes()
        freeze_out = root / 'frozen-result'
        def corrupt_after_read():
            put(a, b'path changed after source snapshot')
        interactive(freeze_out, (a, b), b'1 2 3\n', during=corrupt_after_read)
        assert metadata(freeze_out)['authors'] == [*author_a, author_b[1]]
        put(a, frozen_a)
        swap = put(root / 'swap-result', b'keep destination bytes')
        swap_target = put(root / 'swap-target', b'keep destination bytes')
        def swap_destination():
            swap.unlink()
            swap.symlink_to(swap_target)
        interactive(swap, (a, b), b'1 2 3\n', expected=1, during=swap_destination)
        assert swap.is_symlink() and swap_target.read_bytes() == b'keep destination bytes'
        identical = put(root / 'identical-authors', a.read_bytes())
        interactive(root / 'identical-result', (a, identical), b'2 1\n')
        assert metadata(root / 'identical-result')['authors'] == author_a[::-1]

print(f'goat-merge integration: {COUNT} checks passed')
