#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Module instance identity and first-archive native ownership (#235)."""
import copy
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

from module_snapshot_fixtures import archive, put

EXE = str(Path(sys.argv[1]).resolve())
COUNT = 0


def rewrite(blob, native=None, extra_variant=False):
    """Change excluded informational/native bytes, preserving the main identity."""
    with zipfile.ZipFile(io.BytesIO(blob)) as old:
        entries = {n: old.read(n) for n in old.namelist()}
    metadata = json.loads(entries['module.json'])
    if native is False:
        metadata.pop('native', None)
        entries = {n: v for n, v in entries.items() if not n.startswith('native/')}
    elif isinstance(native, str):
        import hashlib
        name = metadata['native'][0]['library']
        entries[name] = native.encode()
        metadata['native'][0]['hash'] = hashlib.sha256(entries[name]).hexdigest()
    if extra_variant:
        import hashlib
        additional = copy.deepcopy(metadata['native'][0])
        additional['target']['architecture'] = 'aarch64'
        additional['target']['cpuFeatures'] = ['fp', 'neon']
        additional['library'] = 'native/arm.so'
        entries['native/arm.so'] = b'later ARM library'
        additional['hash'] = hashlib.sha256(entries['native/arm.so']).hexdigest()
        metadata['native'].append(additional)
    metadata['description'] = 'Physical copy: metadata excluded from main identity'
    entries['module.json'] = json.dumps(metadata).encode()
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', compression=zipfile.ZIP_STORED) as z:
        for name, data in entries.items():
            z.writestr(name, data)
    return result.getvalue()


def run(project, status=0):
    global COUNT
    COUNT += 1
    process = subprocess.run([EXE, str(project / 'root.gbin'), str(project / 'shared')],
                             capture_output=True, timeout=60)
    assert process.returncode == 0, (COUNT, process.returncode, process.stdout, process.stderr)
    result = json.loads(process.stdout)
    assert result['status'] == status, (COUNT, result)
    if not status:
        assert result['checks'] == len(result['nodes'])
    return result


def node(result, name, version='1'):
    return next(n for n in result['nodes'] if n['name'] == name and n['version'] == version)


with tempfile.TemporaryDirectory(prefix='goat-identity-') as directory:
    base = Path(directory)
    # Two consumers mutate the same external cell. The second reads the first write.
    for copies in ['identical', 'information', 'corrupt-unused']:
        project = base / copies
        shared_blob, shared = archive('shared', value=1)
        a_blob, a = archive('a', dependencies=[shared])
        b_blob, b = archive('b', dependencies=[shared])
        put(project / 'root.gbin', archive('app', dependencies=[a, b])[0])
        for name, blob in [('a', a_blob), ('b', b_blob), ('shared', shared_blob)]:
            put(project / f'modules/{name}/1.gbin', blob)
        physical_copy = shared_blob if copies == 'identical' else rewrite(shared_blob) if copies == 'information' else b'bad'
        put(project / 'shared/shared/1.gbin', physical_copy)
        result = run(project)
        assert len(result['nodes']) == 4
        assert node(result, 'shared')['counter'] == 2
        assert node(result, 'a')['targets'] == node(result, 'b')['targets']

    project = base / 'versions'
    one_blob, one = archive('same', version='1', value=11)
    two_blob, two = archive('same', version='2', value=11)
    a_blob, a = archive('a', dependencies=[one])
    b_blob, b = archive('b', dependencies=[one])
    put(project / 'root.gbin', archive('app', dependencies=[a, b, two])[0])
    for path, blob in [('same/1', one_blob), ('same/2', two_blob), ('a/1', a_blob), ('b/1', b_blob)]:
        put(project / f'modules/{path}.gbin', blob)
    result = run(project)
    assert node(result, 'same', '1')['counter'] == 2
    assert node(result, 'same', '2')['counter'] == 1
    assert node(result, 'same', '1')['hash'] != node(result, 'same', '2')['hash']

    # Identical core identity, deliberately different native contents. Only the first survives.
    full, dependency = archive('library', native=True)
    for variant in ['vm-first', 'native-first', 'different-native', 'corrupt-later', 'fallback-missing', 'extra-fallback']:
        project = base / variant
        put(project / 'root.gbin', archive('app', dependencies=[dependency])[0])
        first = rewrite(full, native=False) if variant == 'vm-first' else full
        if variant not in ['fallback-missing', 'extra-fallback']:
            put(project / 'modules/library/1.gbin', first)
        later = (b'corrupt' if variant == 'corrupt-later' else
                 rewrite(full, native=False) if variant == 'native-first' else
                 rewrite(full, native='later library', extra_variant=variant in ['different-native', 'extra-fallback']))
        put(project / 'shared/library/1.gbin', later)
        result = run(project)
        library = node(result, 'library')
        assert library['hash'] == dependency['hash']
        expected = [] if variant == 'vm-first' else ['later library'] if variant == 'fallback-missing' else ['never OS-load: старый 😀']
        if variant == 'extra-fallback':
            expected = ['later ARM library', 'later library']
        assert library['native'] == expected, (variant, library)

    # First existing mismatch cannot be replaced by a matching later physical copy.
    project = base / 'mismatching-first'
    good_blob, good = archive('library', value=3)
    put(project / 'root.gbin', archive('app', dependencies=[good])[0])
    put(project / 'modules/library/1.gbin', archive('library', value=4)[0])
    put(project / 'shared/library/1.gbin', good_blob)
    run(project, status=2)

    # Conflicting incoming hashes never publish two same-name/version runtime instances.
    project = base / 'hash-conflict'
    other_blob, other = archive('library', value=4)
    a_blob, a = archive('a', dependencies=[good])
    b_blob, b = archive('b', dependencies=[other])
    put(project / 'root.gbin', archive('app', dependencies=[a, b])[0])
    for name, blob in [('a', a_blob), ('b', b_blob), ('library', good_blob)]:
        put(project / f'modules/{name}/1.gbin', blob)
    run(project, status=2)

print(f'Module identity: {COUNT} fixtures passed')
