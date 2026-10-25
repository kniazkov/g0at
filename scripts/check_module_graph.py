#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Whole dependency preflight: independent errors, cached nodes and every path."""
import copy
from module_interface_fixtures import with_external_declarations
import hashlib
import io
import json
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
TARGET = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())['target']
COUNT = 0
SEARCH, HASH, CONFLICT, CYCLE, REFERENCE = range(5)


def image():
    instructions = [(1, 0, 0, 4), (47, 0, 0, 0), (5, 0, 0, 0), (0, 0, 0, 0),
                    (7, 0, 0, 0), (50, 0, 0, 0)]
    files = 64 + len(instructions) * 8
    functions = files + 16
    locations = functions + 32
    end = locations + 48
    return (struct.pack('<8s14I', b'GOATBC01', 1, 64, end, len(instructions), 0, 1, 1, 2,
                        64, files, files, functions, locations, end)
            + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + struct.pack('<4I', 0, len(instructions), 0, 0)
            + struct.pack('<8I', 1, 0, 1, 4, 0, 0, 0xffffffff, 0)
            + struct.pack('<6I', 0, 4, 0, 1, 1, 0)
            + struct.pack('<6I', 4, 2, 0, 2, 1, 1))


IMAGE = image()


def archive(name, version='1', dependencies=(), refs=None, file='main.goat', native=False):
    metadata = dict(copy.deepcopy(BASE), name=name, version=version,
                    functions=[dict(functionId=1, file=0, instruction=1, parameters=[])])
    metadata['files'][0]['name'] = file
    if dependencies:
        metadata['dependencies'] = list(dependencies)
    if refs:
        metadata['externals'] = {key: dict(mutable=False, type='function', functionRef=value)
                                 for key, value in refs.items()}
    libraries = {}
    if native:
        libraries['native/test.so'] = b'Never load this opaque library during preflight'
        metadata['native'] = [dict(library='native/test.so',
                                  hash=hashlib.sha256(libraries['native/test.so']).hexdigest(),
                                  goatAbiVersion=1, target=TARGET,
                                  specializations=[dict(functionId=1, specializationId=1,
                                      symbol='goat_fn_1_v1',
                                      signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                                      when=dict(argumentTypes=[]))])]
    code = with_external_declarations(metadata, IMAGE)
    metadata['mainHash'] = ref.main_hash(metadata, code)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(metadata).encode())
        z.writestr('bytecode.bin', code)
        for path, data in libraries.items():
            z.writestr(path, data)
    return stream.getvalue(), dict(name=name, version=version, hash=metadata['mainHash'])


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)
    return path


def run(root, status=0, mode='module', summary=False):
    global COUNT
    COUNT += 1
    args = [EXE, mode, str(root), str(root)] + ([summary if isinstance(summary, str) else 'summary'] if summary else [])
    def small_stack():
        import resource
        _, hard = resource.getrlimit(resource.RLIMIT_STACK)
        resource.setrlimit(resource.RLIMIT_STACK, (256 * 1024, hard))

    # Also exercise the native process with a stack smaller than recursive traversal needs.
    kwargs = {'preexec_fn': small_stack} if summary and sys.platform != 'win32' else {}
    proc = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', timeout=120,
                          **kwargs)
    assert proc.returncode == 0, (COUNT, proc.returncode, proc.stdout, proc.stderr)
    result = json.loads(proc.stdout)
    assert result['status'] == status, (COUNT, result, status)
    assert bool(result['issues']) == (status == 2), (COUNT, result)
    return result


def node(result, name, version='1'):
    return next(n for n in result['nodes'] if n['name'] == name and n['version'] == version)


def kinds(result):
    return [issue['kind'] for issue in result['issues']]


def chains(result, paths):
    return {tuple(result['nodes'][result['edges'][edge]['to']]['name'] for edge in path)
            for path in paths}


def diagnostic_rows(result):
    """Compare semantic rows, never incidental discovery indices."""
    rows = []
    for row in result['diagnostics']:
        path = [result['nodes'][0]] + [result['nodes'][result['edges'][i]['to']]
                                     for i in row['path']]
        chain = tuple((n['name'], n['version']) for n in path)
        expected = result['edges'][row['path'][-1]]['hash'] if row['path'] else ''
        rows.append((chain, row['kind'], row['reference'], expected, row.get('validation')))
    return rows


def rewrite_metadata(blob, change):
    with zipfile.ZipFile(io.BytesIO(blob)) as source:
        entries = {name: source.read(name) for name in source.namelist()}
    metadata = json.loads(entries['module.json'])
    change(metadata)
    entries['module.json'] = json.dumps(metadata).encode()
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as target:
        for name, data in entries.items():
            target.writestr(name, data)
    return stream.getvalue()


def fref(name, version='1', fid=1):
    return dict(module=name, version=version, functionId=fid)


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-graph-') as temp:
    project = Path(temp) / 'проект'
    root = project / 'root.gbin'

    def install(name, dependencies=(), version='1', **kw):
        blob, record = archive(name, version, dependencies, **kw)
        put(project / 'modules' / name / (version + '.gbin'), blob)
        return record

    def root_archive(dependencies=(), **kw):
        blob, record = archive('app', dependencies=dependencies, **kw)
        put(root, blob)
        return record

    root_archive()
    result = run(root)
    assert result['checks'] == 1
    assert result['diagnostics'] == [] and result['diagnostic_text'] == ''
    c = install('c')
    b = install('b', [c])
    root_archive([b])
    result = run(root)
    assert result['node_count'] == result['checks'] == 3 and result['edge_count'] == 2
    assert node(result, 'app')['metadata']['dependencies'] == [b]
    assert node(result, 'b')['metadata']['dependencies'] == [c]
    assert chains(result, node(result, 'c')['paths']) == {('b', 'c')}
    # A shared node reached again only after it has already been expanded.
    d = install('d', [b])
    root_archive([b, d, c])
    result = run(root)
    assert result['node_count'] == result['checks'] == 4
    assert chains(result, node(result, 'c')['paths']) == {('b', 'c'), ('d', 'b', 'c'), ('c',)}
    assert CYCLE not in kinds(result)
    # Shared invalid/missing nodes have one cached result, preserving late paths.
    cpath = project / 'modules/c/1.gbin'
    cpath.unlink()
    # A dependency's neighboring modules directory cannot change root search paths.
    put(project / 'modules/b/modules/c/1.gbin', archive('c')[0])
    result = run(root, 2)
    assert kinds(result).count(SEARCH) == 1 and node(result, 'c')['search_status'] == 3
    assert chains(result, node(result, 'c')['paths']) == {('b', 'c'), ('d', 'b', 'c'), ('c',)}
    assert node(result, 'c')['attempts'] == 1
    put(cpath, b'corrupt ZIP')
    result = run(root, 2)
    assert kinds(result).count(SEARCH) == 1 and result['checks'] == 4
    assert node(result, 'c')['search_status'] == 4
    # A locally valid but incorrectly named transitive candidate cannot satisfy C.
    put(cpath, archive('imposter')[0])
    result = run(root, 2)
    assert node(result, 'c')['search_status'] == 5 and kinds(result).count(SEARCH) == 1
    c = install('c')
    # Two versions remain independent, regardless of app/library defaults.
    c2 = install('c', version='2')
    root_archive([b, c2])
    result = run(root)
    assert result['checks'] == 4 and node(result, 'c', '2')['hash'] == c2['hash']
    # Conflicts retain each request, even when one uses the validated actual hash.
    wrong = dict(c, hash='0' * 64)
    d = install('d', [wrong])
    root_archive([b, d])
    result = run(root, 2)
    assert HASH in kinds(result) and CONFLICT in kinds(result)
    issue = next(i for i in result['issues'] if i['kind'] == CONFLICT)
    edges = [result['edges'][issue[k]] for k in ('edge', 'other_edge')]
    assert {e['hash'] for e in edges} == {c['hash'], wrong['hash']}
    assert set.union(*(chains(result, e['paths']) for e in edges)) == {('b', 'c'), ('d', 'c')}
    # All requests remain available, not merely the two edges which discover conflict.
    e = install('e', [c])
    root_archive([b, d, e])
    result = run(root, 2)
    assert chains(result, node(result, 'c')['paths']) == {('b', 'c'), ('d', 'c'), ('e', 'c')}
    incoming = [edge for edge in result['edges'] if result['nodes'][edge['to']]['name'] == 'c']
    assert sorted(edge['hash'] for edge in incoming) == sorted([c['hash'], c['hash'], wrong['hash']])
    cpath.unlink()
    result = run(root, 2)
    assert SEARCH in kinds(result) and CONFLICT in kinds(result)
    c = install('c')
    bwrong = install('b', [wrong])
    root_archive([bwrong, d])
    result = run(root, 2)
    assert kinds(result).count(HASH) == 2 and CONFLICT not in kinds(result)
    # Expected mismatch does not hide independently discoverable missing child.
    missing = dict(name='missing', version='1', hash='1' * 64)
    badc = install('c', [missing])
    root_archive([bwrong])
    result = run(root, 2)
    assert HASH in kinds(result) and SEARCH in kinds(result)
    # Correctly hashed local archives may still encode cyclic (wrong-hash) edges.
    self_edge = dict(name='app', version='1', hash='0' * 64)
    root_archive([self_edge])
    result = run(root, 2)
    assert result['checks'] == result['node_count'] == 1
    assert HASH in kinds(result) and CYCLE in kinds(result)
    assert result['edges'][0]['to'] == 0 and result['edges'][0]['paths'] == [[0]]
    bcycle = install('b', [self_edge])
    root_archive([bcycle])
    result = run(root, 2)
    assert result['checks'] == 2 and CYCLE in kinds(result)
    cycle = next(i for i in result['issues'] if i['kind'] == CYCLE)
    assert chains(result, result['edges'][cycle['edge']]['paths']) == {('b', 'app')}
    # A broken root is retained as a failed node and never guessed from search paths.
    put(root, b'broken root')
    result = run(root, 2)
    assert result['node_count'] == 1 and kinds(result) == [SEARCH]
    # Transitive true function owner; intermediate aliases do not flatten metadata.
    c = install('c')
    b = install('b', [c], refs={'alias': fref('c')})
    root_archive([b], refs={'alias': fref('c')})
    result = run(root)
    assert node(result, 'app')['metadata']['dependencies'] == [b]
    # Sibling-only C is not reachable from B; global lookup cannot satisfy its ref.
    b = install('b', refs={'alias': fref('c')})
    root_archive([b, c])
    result = run(root, 2)
    assert kinds(result) == [REFERENCE]
    assert result['nodes'][result['issues'][0]['node']]['name'] == 'b'
    # Two versions/functions aren't interchangeable; collect independent bad exports.
    b = install('b', [c], refs={'badId': fref('c', fid=7), 'badVersion': fref('c', '2')})
    root_archive([b, c2])
    result = run(root, 2)
    assert kinds(result).count(REFERENCE) == 2, result
    assert {i['reference'] for i in result['issues']} == {'badId', 'badVersion'}
    # Independent malformed, missing and bad references survive the same pass.
    broken = install('broken')
    put(project / 'modules/broken/1.gbin', b'corrupt')
    root_archive([b, broken, missing])
    result = run(root, 2)
    assert kinds(result).count(SEARCH) == 2 and kinds(result).count(REFERENCE) == 2
    # An incomplete owner's graph does not hide a known invalid target function.
    b = install('b', [c, missing], refs={'badId': fref('c', fid=7), 'blocked': fref('unseen')})
    root_archive([b])
    result = run(root, 2)
    assert SEARCH in kinds(result) and REFERENCE in kinds(result)
    assert node(result, 'b')['references_skipped'] == 1
    # Preflight only validates native bytes, never invokes an OS loader.
    opaque = install('opaque', native=True)
    root_archive([opaque])
    assert run(root)['checks'] == 2
    # Source roots consume syntax imports/includes/defaults without transitive flattening.
    desc = put(project / 'app.gmod', json.dumps(dict(formatVersion=1, name='source', version='1',
        main='main.goat', dependencies={'b': '1', 'unused': 'not-installed'})))
    b = install('b', [c])
    put(project / 'main.goat', 'include "own.goat"; if(false){import "b";}')
    put(project / 'own.goat', 'func(){import "c@2";}')
    result = run(desc, mode='gmod')
    assert result['node_count'] == 4 and result['checks'] == 3
    assert len([e for e in result['edges'] if e['from'] == 0]) == 2
    cpath.unlink()
    result = run(desc, 2, mode='gmod')
    assert node(result, 'c')['search_status'] == 3
    put(project / 'main.goat', 'import "c"; import "c@2";')
    run(desc, 105, mode='gmod')
    # Empty roots and direct source imports need no fabricated module identity.
    put(project / 'main.goat', '')
    result = run(project / 'main.goat', mode='goat')
    assert result['checks'] == 0 and result['node_count'] == 1
    # Source dependency analysis alone cannot authorize publication of a named root.
    old_app = install('app')
    b = install('b', [old_app])
    put(project / 'main.goat', 'import "b@1";')
    assert run(project / 'main.goat', mode='goat')['checks'] == 2
    root_archive([b])
    result = run(root, 2)
    assert CYCLE in kinds(result) and HASH in kinds(result)
    assert result['checks'] == 2  # final root wins over the old installed app
    # Full-chain diagnostics, not graph discovery order; every conflict request
    # must appear, including two paths expecting the actual hash.
    shared = install('shared')
    first = install('first', [shared])
    second = install('second', [dict(shared, hash='0' * 64)])
    third = install('third', [shared])
    fourth = install('fourth', [dict(shared, hash='1' * 64)])
    source = project / 'diagnostics.goat'
    put(source, 'import "third@1"; import "second@1"; import "first@1"; import "fourth@1";')
    result = run(source, 2, mode='goat')
    baseline = diagnostic_rows(result)
    conflicts = [r for r in baseline if r[1] == CONFLICT]
    assert len(conflicts) == 4, conflicts
    assert {r[0][-2][0] for r in conflicts} == {'first', 'second', 'third', 'fourth'}
    assert {r[3] for r in conflicts} == {shared['hash'], '0' * 64, '1' * 64}
    assert [r[0] for r in baseline] == sorted(r[0] for r in baseline)
    assert all(h in result['diagnostic_text'] for h in (shared['hash'], '0' * 64, '1' * 64))
    put(source, 'import "fourth@1"; import "first@1"; import "second@1"; import "third@1";')
    reordered = run(source, 2, mode='goat')
    assert diagnostic_rows(reordered) == baseline
    assert reordered['diagnostic_text'] == result['diagnostic_text']
    # A late incoming chain is not lost because the bad node was already cached.
    late = install('late', [first])
    put(source, 'import "late@1"; import "second@1"; import "first@1"; import "third@1";')
    result = run(source, 2, mode='goat')
    conflicts = [r for r in diagnostic_rows(result) if r[1] == CONFLICT]
    assert len(conflicts) == 4
    assert any(tuple(n[0] for n in r[0]) == ('<source>', 'late', 'first', 'shared')
               for r in conflicts)
    # Multiple positioned schema failures in a shared corrupt archive: all paths
    # preserve the same owned field/entry bytes after inputs/search are released.
    blob, shared = archive('shared')
    blob = rewrite_metadata(blob, lambda m: m.update(name=False, version=42))
    put(project / 'modules/shared/1.gbin', blob)
    result = run(source, 2, mode='goat')
    rows = diagnostic_rows(result)
    schema = [r for r in rows if r[4] and r[4]['state'] == 1 and
              r[4]['field'] in ('/name', '/version')]
    assert len(schema) == 8, schema
    paths = sorted({r[0] for r in schema})
    assert len(paths) == 4 and result['checks'] == 5
    for chain in paths:
        errors = [r[4] for r in schema if r[0] == chain]
        assert [e['field'] for e in errors] == ['/name', '/version'], errors
        assert all(e['entry'] == 'module.json' for e in errors)
    assert any(r[4] and r[4]['state'] == 2 for r in rows)
    assert [r[0] for r in rows] == sorted(r[0] for r in rows)
    assert '/name' in result['diagnostic_text'] and '/version' in result['diagnostic_text']
    assert run(source, 2, mode='goat')['diagnostic_text'] == result['diagnostic_text']
    # Sorting references uses their actual export positions, not JSON key order.
    shared = install('shared')
    refs = {'zebra': fref('shared', fid=8), 'alpha': fref('shared', fid=9)}
    first = install('first', [shared], refs=refs)
    root_archive([first, missing])
    result = run(root, 2)
    baseline = diagnostic_rows(result)
    assert [r[2] for r in baseline if r[1] == REFERENCE] == ['alpha', 'zebra']
    first2 = install('first', [shared], refs=dict(reversed(list(refs.items()))))
    assert first == first2  # Object key permutations do not alter identity.
    reordered = run(root, 2)
    assert diagnostic_rows(reordered) == baseline
    assert reordered['diagnostic_text'] == result['diagnostic_text']
    # Export keys are length-bearing strings; embedded NUL must not erase suffixes.
    zero_refs = {'alpha\x00z': fref('shared', fid=8), 'alpha\x00a': fref('shared', fid=9)}
    first = install('first', [shared], refs=zero_refs)
    root_archive([first])
    nul_result = run(root, 2)
    names = [r[2] for r in diagnostic_rows(nul_result) if r[1] == REFERENCE]
    assert names == ['alpha\x00a', 'alpha\x00z'], names
    assert all(r['reference_length'] == 7 for r in nul_result['diagnostics'])
    assert 'alpha\\x00a' in nul_result['diagnostic_text']
    assert 'alpha\\x00z' in nul_result['diagnostic_text']
    # Full chain ordering precedes error kind; all same-chain errors use kind order.
    keys = [(r[0], r[1]) for r in baseline]
    assert keys == sorted(keys)
    # Cyclic diagnostics are finite, include the terminal repeated node, and repeat stably.
    first = install('first', [dict(name='app', version='1', hash='0' * 64)])
    root_archive([first])
    result = run(root, 2)
    cycles = [r for r in diagnostic_rows(result) if r[1] == CYCLE]
    assert len(cycles) == 1 and tuple(n[0] for n in cycles[0][0]) == ('app', 'first', 'app')
    assert run(root, 2)['diagnostic_text'] == result['diagnostic_text']
    # A canonical DFS back edge can have another noncyclic path to its consumer;
    # only paths which really repeat the terminal node are cycle witnesses.
    cyclea = install('cyclea', [dict(name='cycleb', version='1', hash='0' * 64)])
    cycleb = install('cycleb', [dict(name='cyclea', version='1', hash='1' * 64)])
    root_archive([cycleb, cyclea])
    result = run(root, 2)
    cycles = [r for r in diagnostic_rows(result) if r[1] == CYCLE]
    assert cycles and all(r[0][-1] in r[0][:-1] for r in cycles), cycles
    assert {tuple(n[0] for n in r[0]) for r in cycles} == {
        ('app', 'cyclea', 'cycleb', 'cyclea'),
        ('app', 'cycleb', 'cyclea', 'cycleb')}
    assert len(cycles) == 2
    # Failed/skipped snapshots are retained without any observer callback.
    put(root, b'bad ZIP')
    observed = run(root, 2)
    unobserved = run(root, 2, mode='module-null')
    assert unobserved['checks'] == 0 and unobserved['nodes'][0]['validation_count'] > 0
    assert diagnostic_rows(unobserved) == diagnostic_rows(observed)
    assert unobserved['diagnostic_text'] == observed['diagnostic_text']
    # ZIP entry names are bounded bytes, not assumed UTF-8 or NUL-terminated.
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', b'{}')
        z.writestr('bytecode.bin', b'')
        z.writestr('native/bad_.so', b'opaque')
    bad_name = b'native/bad\xff.so'
    put(root, stream.getvalue().replace(b'native/bad_.so', bad_name))
    result = run(root, 2)
    assert any(bytes(r['validation']['entry_bytes']) == bad_name
               for r in result['diagnostics'] if r.get('validation')), result
    assert r'native/bad\xff.so' in result['diagnostic_text'], result
    # Field and export strings retain control characters and backslashes safely.
    key = 'line\n\\suffix'
    blob, _ = archive('app', refs={key: fref('absent')})
    put(root, rewrite_metadata(blob, lambda m: m['externals'][key].update(mutable=42)))
    result = run(root, 2)
    assert any(key in r['validation']['field']
               for r in result['diagnostics'] if r.get('validation')), result
    assert r'line\x0a\\suffix' in result['diagnostic_text'], result
    # Preserve ordinary Unicode while neutralizing display controls in exports.
    blob, _ = archive('app', refs={key: fref('absent') for key in
                                  ('café', 'x\u0085z', 'x\u202ez', 'x\u2069z')})
    put(root, blob)
    result = run(root, 2)
    assert 'café' in result['diagnostic_text']
    assert all(escaped in result['diagnostic_text'] for escaped in
               (r'x\u0085z', r'x\u202ez', r'x\u2069z'))
    # Physical names with controls are permitted by POSIX, not by Windows.
    if sys.platform != 'win32':
        label = put(project / 'line\n\\label.gbin', b'bad ZIP')
        result = run(label, 2)
        assert r'line\x0a\\label.gbin' in result['diagnostic_text'], result
    # Independent hash-check types sort before locations (native entry before
    # module.json even though lexical entry ordering alone would reverse them).
    blob, _ = archive('app', native=True)
    def damage_hashes(metadata):
        metadata['mainHash'] = '0' * 64
        metadata['native'][0]['hash'] = '0' * 64
    put(root, rewrite_metadata(blob, damage_hashes))
    result = run(root, 2)
    errors = [r['validation'] for r in result['diagnostics'] if r.get('validation')
              and r['validation']['container_status'] in (12, 13)]
    assert [r['container_status'] for r in errors] == [13, 12], errors
    assert [r['entry'] for r in errors] == ['native/test.so', 'module.json'], errors
    # Deep chain: no artificial depth limit; path traversal/free also iterative.
    dep = None
    depth = 2500
    for i in reversed(range(depth)):
        dep = install(f'deep{i}', [dep] if dep else [])
    root_archive([dep])
    result = run(root, summary=True)
    assert result['node_count'] == result['checks'] == depth + 1
    assert result['last_path_length'] == depth
    # The formatter/report pass must also survive a deep failing chain on a small stack.
    dep = dict(name='deepmissing', version='1', hash='0' * 64)
    for i in reversed(range(depth)):
        dep = install(f'deep{i}', [dep])
    root_archive([dep])
    result = run(root, 2, summary='diagnostics')
    assert result['node_count'] == depth + 2 and result['checks'] == depth + 1
    assert len(result['diagnostics']) == 1
    assert len(result['diagnostics'][0]['path']) == depth + 1
    assert 'deepmissing' in result['diagnostic_text']
    # Exponentially many paths must not be materialized during validation.
    deps = [install('bottom')]
    levels = 32
    for i in reversed(range(levels)):
        deps = [install(f'layer{i}a', deps), install(f'layer{i}b', deps)]
    root_archive([*deps, install("zisolated")])
    result = run(root, summary=True)
    assert result['node_count'] == result['checks'] == 2 * levels + 3
    assert result['last_path_length'] == levels + 1
print(f'{COUNT} module graph fixtures passed')
