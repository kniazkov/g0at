#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Independent exact bindings for each consumer, not a whole-graph walker (#189)."""
import copy
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
COUNT = 0


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


def archive(name, version, dependencies=(), file='main.goat'):
    metadata = dict(copy.deepcopy(BASE), name=name, version=version,
                    functions=[dict(functionId=1, file=0, instruction=1, parameters=[])])
    metadata['files'][0]['name'] = file
    if dependencies:
        metadata['dependencies'] = list(dependencies)
    metadata['mainHash'] = ref.main_hash(metadata, IMAGE)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(metadata).encode())
        z.writestr('bytecode.bin', IMAGE)
    return stream.getvalue(), dict(name=name, version=version, hash=metadata['mainHash'])


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)
    return path


def run(mode, root, consumer=None, status=0, search_status=0):
    global COUNT
    COUNT += 1
    proc = subprocess.run([EXE, mode, str(root), str(consumer or root)],
                          capture_output=True, text=True, encoding='utf-8', timeout=30)
    assert proc.returncode == 0, (COUNT, proc.stdout, proc.stderr)
    result = json.loads(proc.stdout)
    assert result['status'] == status, (COUNT, result, status)
    if status:
        assert result['search_status'] == search_status, (COUNT, result)
        assert 'dependencies' not in result, result
    return result


def records(result):
    return result['dependencies']


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-dependencies-') as temp:
    project = Path(temp) / 'проект'
    desc = put(project / 'app.gmod', '')
    main = put(project / 'main.goat', '')

    def describe(defaults=None):
        data = dict(formatVersion=1, name='app', version='1', main='main.goat')
        if defaults is not None:
            data['dependencies'] = defaults
        put(desc, json.dumps(data))

    def install(name, version, dependencies=(), file='main.goat'):
        blob, record = archive(name, version, dependencies, file)
        return put(project / 'modules' / name / (version + '.gbin'), blob), record

    math1_path, math1 = install('math', '1')
    math2_path, math2 = install('math', '2')
    lib_path, lib = install('library', '7', [math1])
    describe({'math': '2', 'library': '7', 'never-installed': '4'})
    put(main, 'import "library"; if(false){import "math";} import "math@1"; import "math";')
    app = run('gmod', desc)
    assert records(app) == [lib, math1, math2], app
    assert app['unused'] == 1
    # Default math@2 in the application must not redirect the library's math@1.
    library = run('module', desc, lib_path)
    assert records(library) == [math1], library
    assert records(run('module', desc, math1_path)) == []
    assert records(run('module', desc, math2_path)) == []
    assert app['snapshots'][0]['dependencies'] == [math1]
    # Explicit version only affects that occurrence; unused default isn't searched.
    describe({'math': 'not-installed'})
    put(main, 'import "math@1"; import "math@1";')
    result = run('gmod', desc)
    assert records(result) == [math1] and result['unused'] == 1
    # Include imports are direct; unreachable/default and explicit coexist unchanged.
    describe({'math': '2'})
    put(main, 'include "own.goat"; import "math@1";')
    put(project / 'own.goat', 'func(){import "math";}')
    assert records(run('gmod', desc)) == [math1, math2]
    # Libraries do not contribute defaults, nor does any available/explicit version.
    describe()
    put(main, 'import "math@1"; import "math";')
    run('gmod', desc, status=105)
    # Application records retain only their own imports, never library transitive edges.
    describe({'library': '7'})
    put(main, 'import "library";')
    app = run('gmod', desc)
    assert records(app) == [lib], app
    assert app['snapshots'][0]['dependencies'] == [math1]
    # Missing transitive module does not make this direct binding primitive recurse.
    math1_path.unlink()
    assert records(run('gmod', desc)) == [lib]
    run('module', desc, lib_path, status=2, search_status=3)
    math1_path, _ = install('math', '1')
    # A -> B -> C is represented by independent owned per-consumer edge sets.
    app_path, _ = install('application', '1', [lib])
    assert records(run('module', desc, app_path)) == [lib]
    assert records(run('module', desc, lib_path)) == [math1]
    # Corrupt replacement of transitive C is detected when binding B's own edges.
    put(math1_path, b'not an archive')
    run('module', desc, lib_path, status=2, search_status=4)
    math1_path, _ = install('math', '1')
    # Different valid build of the same pair must fail B's exact recorded hash.
    _, replacement = install('math', '1', file='replacement.goat')
    assert replacement != math1
    describe({'math': '1'})
    put(main, 'import "math";')
    assert records(run('gmod', desc)) == [replacement]
    failure = run('module', desc, lib_path, status=2, search_status=6)
    assert failure['index'] == 0 and len(failure['attempted']) == 1
    assert Path(failure['attempted'][0]).parts[-2:] == ('math', '1.gbin')
    # Even two consumers using the same version keep independently committed hashes.
    second_path, _ = install('second', '1', [replacement])
    assert records(run('module', desc, second_path)) == [replacement]
    run('module', desc, lib_path, status=2, search_status=6)
    # Input record order isn't a priority; output is the canonical direct set.
    unordered_path, _ = install('unordered', '1', [math2, replacement])
    ordered_path, _ = install('ordered', '1', [replacement, math2])
    assert records(run('module', desc, unordered_path)) == [replacement, math2]
    assert records(run('module', desc, ordered_path)) == [replacement, math2]
    # Failure after a valid first edge exposes no partial result and releases snapshots.
    missing = dict(name='zmissing', version='1', hash='0' * 64)
    partial_path, _ = install('partial', '1', [replacement, missing])
    failure = run('module', desc, partial_path, status=2, search_status=3)
    assert failure['index'] == 1
    # Empty consumers need no archive lookup at all.
    describe({'missing': '1'})
    put(main, '')
    result = run('gmod', desc)
    assert records(result) == [] and result['unused'] == 1
    # Source input has only explicit versions, and its result owns verified hashes.
    put(main, 'import "math@2"; import "math@1";')
    assert records(run('source', main)) == [replacement, math2]

print(f'Per-consumer dependency bindings: {COUNT} checks passed')
