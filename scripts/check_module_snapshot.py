#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Pinned graph snapshots survive archive replacement/deletion before actual VM execution."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref
EXE = str(Path(sys.argv[1]).resolve())
BASE = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
TARGET = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())['target']
COUNT = 0


def image(value, label, files=1, throwing=False, native=False, dormant_import=None):
    descriptors, payload = [], bytearray()

    def data(blob, kind, count):
        payload.extend(bytes((-len(payload)) % 8))
        index = len(descriptors)
        descriptors.append((len(payload), len(blob), kind, count))
        payload.extend(blob)
        return index

    def string(text):
        return data((text + '\0').encode('utf-32-le'), 1, len(text))

    x, label_key, label_value = string('x'), string('label'), string(label)
    import_string = string(dormant_import) if dormant_import else None
    instructions, file_records, functions, locations = [], [], [], []
    for file in range(files):
        start = len(instructions)
        declaration = data(struct.pack('<4I', 0x4445434c, x, file, 0), 3, 1)
        instructions.extend([(10, 0, 0, value + file), (69, 0, 0, declaration),
                             (13, 0, 0, label_value), (16, 0, 0, label_key)])
        if dormant_import and file == 0:
            instructions.extend([(3, 0, 0, len(instructions) + 3),
                                 (74, 0, 0, import_string), (5, 0, 0, 0)])
        if native and file == 0:
            instruction = len(instructions) + 1
            instructions.extend([(1, 0, 0, len(instructions) + 4),
                                 (47, 0, 0, 0), (5, 0, 0, 0), (2, 0, 0, 0),
                                 (7, 0, 0, 0), (50, 0, 0, 0)])
            functions.append((1, file, instruction, instruction + 3, 0, 0, 0xffffffff, 0))
            locations.extend([(start, instruction + 3 - start, file, 1, 1, 0),
                              (instruction + 3, 2, file, 2, 1, 1)])
        else:
            if throwing:
                instructions.extend([(10, 0, 0, 73), (55, 0, 0, 0)])
            instructions.append((2, 0, 0, 0))
            locations.append((start, len(instructions) - start, file, 1, 1, 0))
        file_records.append((start, len(instructions) - start, start, 0))
    od = 64 + len(instructions) * 8
    of = od + len(descriptors) * 16
    on = of + files * 16
    ol = on + len(functions) * 32
    start = ol + len(locations) * 24
    header = struct.pack('<8s14I', b'GOATBC01', 1, 64, start + len(payload),
                         len(instructions), len(descriptors), files, len(functions), len(locations),
                         64, od, of, on, ol, start)
    code = (header + b''.join(struct.pack('<BBHI', *v) for v in instructions)
            + b''.join(struct.pack('<4I', *v) for v in descriptors)
            + b''.join(struct.pack('<4I', *v) for v in file_records)
            + b''.join(struct.pack('<8I', *v) for v in functions)
            + b''.join(struct.pack('<6I', *v) for v in locations) + payload)
    return code, file_records, functions


def archive(name, version='1', dependencies=(), value=100, label='старый 😀',
            files=1, throwing=False, native=False, dormant_import=None):
    code, file_records, functions = image(value, label, files, throwing, native, dormant_import)
    metadata = dict(copy.deepcopy(BASE), name=name, version=version, main=files - 1,
                    files=[dict(name=f'absent-{i}.goat', entry=v[2], external=False)
                           for i, v in enumerate(file_records)])
    if dependencies:
        metadata['dependencies'] = list(dependencies)
    if functions:
        metadata['functions'] = [dict(functionId=f[0], file=f[1], instruction=f[2], parameters=[])
                                 for f in functions]
    libraries = {}
    if native:
        libraries['native/opaque.so'] = ('never OS-load: ' + label).encode()
        metadata['native'] = [dict(library='native/opaque.so',
                                  hash=hashlib.sha256(libraries['native/opaque.so']).hexdigest(),
                                  goatAbiVersion=1, target=TARGET,
                                  specializations=[dict(functionId=1, specializationId=1,
                                      symbol='goat_fn_1_v1',
                                      signature=dict(id='goat.adapter.v1', arguments=[], result='null'),
                                      when=dict(argumentTypes=[]))])]
    metadata['mainHash'] = ref.main_hash(metadata, code)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as z:
        z.writestr('module.json', json.dumps(metadata, ensure_ascii=False).encode())
        z.writestr('bytecode.bin', code)
        for path, blob in libraries.items():
            z.writestr(path, blob)
    record = dict(name=name, version=version, hash=metadata['mainHash'])
    return stream.getvalue(), record


def put(path, blob):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)
    return path


def run(root, status=0, mutate=None, mode='run', small_stack=False):
    global COUNT
    COUNT += 1
    args = [EXE, str(root), 'sync' if mutate else mode]
    kwargs = {}
    if small_stack and sys.platform != 'win32':
        def stack():
            import resource
            _, hard = resource.getrlimit(resource.RLIMIT_STACK)
            resource.setrlimit(resource.RLIMIT_STACK, (256 * 1024, hard))
        kwargs['preexec_fn'] = stack
    if mutate:
        # READY establishes that every archive is pinned and no initializer has run.
        with subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, **kwargs) as process:
            watchdog = threading.Timer(120, process.kill)
            watchdog.daemon = True
            watchdog.start()
            try:
                ready = process.stdout.readline()
                assert ready == b'READY\n', (COUNT, ready)
                mutate()
                stdout, stderr = process.communicate(b'G', timeout=120)
                assert process.returncode == 0, (COUNT, process.returncode, stdout, stderr)
            finally:
                watchdog.cancel()
                if process.poll() is None:
                    process.kill()
    else:
        process = subprocess.run(args, capture_output=True, timeout=120, **kwargs)
        stdout, stderr = process.stdout, process.stderr
        assert process.returncode == 0, (COUNT, process.returncode, stdout, stderr)
        if not status:
            assert stdout.startswith(b'READY\n'), (COUNT, stdout)
            stdout = stdout.split(b'\n', 1)[1]
    result = json.loads(stdout)
    assert result['status'] == status, (COUNT, status, result)
    if not status:
        assert result['checks'] == len(result['nodes'])
    else:
        assert 'nodes' not in result
        if mode != 'no-diagnostics':
            assert result['issues'] > 0
    return result


def node(result, name, version='1'):
    return next(n for n in result['nodes'] if n['name'] == name and n['version'] == version)


def fixture(project, **kwargs):
    return put(project / 'root.gbin', archive('app', **kwargs)[0])


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-snapshot-') as directory:
    base = Path(directory)

    for files in [1, 3, 11]:
        root = fixture(base / f'files-{files}', files=files)
        result = run(root)
        assert node(result, 'app')['values'] == list(range(100, 100 + files))
        assert node(result, 'app')['label'] == 'старый 😀'

    for mutation in ['replace', 'delete', 'corrupt']:
        project = base / mutation
        c_blob, c = archive('c', value=30, native=True)
        b_blob, b = archive('b', dependencies=[c], value=20, dormant_import='c@1')
        root = fixture(project, dependencies=[b], value=10, files=3, native=True)
        put(project / 'modules/b/1.gbin', b_blob)
        put(project / 'modules/c/1.gbin', c_blob)
        old_native = hashlib.sha256('never OS-load: старый 😀'.encode()).hexdigest()

        def mutate():
            if mutation == 'replace':
                new_c_blob, new_c = archive('c', value=300, native=True, label='new')
                new_b_blob, new_b = archive('b', dependencies=[new_c], value=200, label='new')
                put(project / 'modules/c/1.gbin', new_c_blob)
                put(project / 'modules/b/1.gbin', new_b_blob)
                fixture(project, dependencies=[new_b], value=1000, files=3, native=True, label='new')
            elif mutation == 'delete':
                for path in project.rglob('*.gbin'):
                    path.unlink()
            else:
                for path in project.rglob('*.gbin'):
                    path.write_bytes(b'not a zip')

        result = run(root, mutate=mutate)
        assert node(result, 'app')['values'] == [10, 11, 12]
        assert node(result, 'b')['values'] == [20] and node(result, 'c')['values'] == [30]
        assert node(result, 'app')['native'] == [old_native]
        assert node(result, 'c')['native'] == [old_native]
        if mutation == 'replace':
            fresh = run(root)
            assert node(fresh, 'app')['values'] == [1000, 1001, 1002]
            assert node(fresh, 'b')['values'] == [200] and node(fresh, 'c')['values'] == [300]
            assert node(fresh, 'c')['native'] != [old_native]
        else:
            # The search constructor needs an existing root; restore only root after deletion.
            if mutation == 'delete':
                fixture(project, dependencies=[b])
            run(root, status=2)

    project = base / 'diamond'
    c_blob, c = archive('c', value=3)
    b_blob, b = archive('b', dependencies=[c], value=2)
    d_blob, d = archive('d', dependencies=[c], value=4)
    root = fixture(project, dependencies=[b, d], value=1)
    for name, blob in [('b', b_blob), ('c', c_blob), ('d', d_blob)]:
        put(project / f'modules/{name}/1.gbin', blob)
    put(project / 'modules/unreachable/1.gbin', b'corrupt but not reachable')
    result = run(root)
    assert result['checks'] == 4
    assert node(result, 'b')['targets'] == node(result, 'd')['targets']
    assert len(node(result, 'app')['targets']) == 2

    project = base / 'versions'
    one_blob, one = archive('same', version='1', value=11)
    two_blob, two = archive('same', version='2', value=22)
    b_blob, b = archive('b', dependencies=[one], value=1)
    root = fixture(project, dependencies=[b, two])
    for path, blob in [('same/1', one_blob), ('same/2', two_blob), ('b/1', b_blob)]:
        put(project / f'modules/{path}.gbin', blob)
    result = run(root, mutate=lambda: shutil.rmtree(project / 'modules'))
    assert node(result, 'same', '1')['values'] == [11]
    assert node(result, 'same', '2')['values'] == [22]

    for failure in ['missing', 'corrupt', 'hash', 'identity']:
        project = base / f'bad-{failure}'
        c_blob, c = archive('c')
        if failure == 'hash':
            c['hash'] = '0' * 64
        b_blob, b = archive('b', dependencies=[c], dormant_import='c@1')
        root = fixture(project, dependencies=[b], native=True)
        put(project / 'modules/b/1.gbin', b_blob)
        if failure != 'missing':
            put(project / 'modules/c/1.gbin', b'bad' if failure == 'corrupt' else
                archive('wrong')[0] if failure == 'identity' else c_blob)
        run(root, status=2)
        run(root, status=2, mode='no-diagnostics')

    root = fixture(base / 'throw', throwing=True)
    result = run(root, mutate=lambda: root.unlink())
    assert node(result, 'app')['failures'] == 1
    assert node(result, 'app')['values'] == [100]

    project = base / 'deep'
    dependency = None
    for i in reversed(range(512)):
        blob, dependency = archive(f'm{i}', dependencies=[dependency] if dependency else [], value=i)
        put(project / f'modules/m{i}/1.gbin', blob)
    root = fixture(project, dependencies=[dependency])
    result = run(root, small_stack=True)
    assert result['checks'] == 513
    assert node(result, 'm511')['values'] == [511]

print(f'Module snapshots: {COUNT} fixtures passed')
