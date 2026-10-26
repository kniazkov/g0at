#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Module-v1 native producer integration; real compiler and immutable archive checks."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

EXE = str(Path(sys.argv[1]).resolve())
CC = shutil.which(sys.argv[2]) or sys.argv[2]
passed = 0


def invoke(*args, success=True, env=None):
    global passed
    run = subprocess.run([EXE, *map(str, args)], capture_output=True, timeout=120, env=env)
    assert (run.returncode == 0) == success, (args, run.returncode, run.stdout, run.stderr)
    passed += 1
    return run


with tempfile.TemporaryDirectory(prefix='goat-module-build-') as temporary:
    root = Path(temporary)
    def project(name, source, native=None, files=None):
        directory = root / name
        directory.mkdir()
        (directory / 'main.goat').write_text(source, encoding='utf-8')
        for path, content in (files or {}).items():
            (directory / path).write_text(content, encoding='utf-8')
        descriptor = {'formatVersion': 1, 'name': name, 'version': '1', 'main': 'main.goat'}
        if native is not None:
            descriptor['native'] = native
        path = directory / 'module.gmod'
        path.write_text(json.dumps(descriptor), encoding='utf-8')
        return path, directory / 'module.gbin'

    def build(project, mode='descriptor', target='descriptor', cc=CC, success=True):
        if success and project[1].exists():
            project[1].unlink()
        return invoke('native-build', *project, mode, target, cc, success=success)

    def contents(path):
        with zipfile.ZipFile(path) as archive:
            doc = json.loads(archive.read('module.json'))
            return doc, {name: archive.read(name) for name in archive.namelist()}

    p = project('numeric', 'external square = func(x) { return x*x; }; println(square(7));',
                {'mode': 'required'})
    run = build(p)
    assert not run.stdout, run.stdout  # Building never runs the initializer.
    doc, files = contents(p[1])
    assert len(doc['native']) == 1, doc
    variant = doc['native'][0]
    assert variant['cCompiler']['name'] and variant['cCompiler']['version']
    assert hashlib.sha256(files[variant['library']]).hexdigest() == variant['hash']
    assert variant['target']['cpuFeatures'] in [['sse2'], ['fp', 'neon'], ['i686']], variant
    assert variant['target']['runtime']['minimumVersion']
    assert variant['goatAbiVersion'] == 1
    fid = doc['externals']['square']['functionId']
    assert all(item['functionId'] == fid for item in variant['specializations'])
    output = invoke('binary', p[1]).stdout
    assert output == ('49' + os.linesep).encode(), output
    main_hash = doc['mainHash']
    bytecode = files['bytecode.bin']
    saved = root / ('saved.dll' if os.name == 'nt' else 'saved.so')
    saved.write_bytes(files[variant['library']])
    build(p, 'off', cc=str(root / 'missing-compiler'))
    plain, plain_files = contents(p[1])
    assert not plain.get('native') and plain['mainHash'] == main_hash
    assert plain_files['bytecode.bin'] == bytecode

    p = project('empty', 'println("must not run");', {'mode': 'required'})
    run = build(p, success=False)
    assert not run.stdout and not p[1].exists()
    assert b'own specialization' in run.stderr, run.stderr
    build(p, 'off', cc=str(root / 'missing'))
    assert not contents(p[1])[0].get('native')
    build(p, 'auto', 'linux-x86_64-gnu', str(root / 'absent'), success=False)

    p = project('included', 'include "helper.goat";', {'mode': 'required'},
                {'helper.goat': 'const twice = func(x) { return x*2; }; println(twice(21));'})
    build(p)
    doc, _ = contents(p[1])
    assert doc['native'][0]['specializations']
    functions = {f['functionId']: f for f in doc['functions']}
    assert all(functions[s['functionId']]['file'] == 1 for s in doc['native'][0]['specializations'])
    output = invoke('binary', p[1]).stdout
    assert output == ('42' + os.linesep).encode(), output

    p = project('missing', 'const f=func(x){return x+1;}; f(3);')
    run = build(p, cc=str(root / 'absent'))
    assert b'warning:' in run.stderr and not contents(p[1])[0].get('native')
    previous = p[1].read_bytes()
    build(p, 'required', cc=str(root / 'absent'), success=False)
    assert p[1].read_bytes() == previous
    build(p, 'auto', 'not-a-supported-target', success=False)
    assert p[1].read_bytes() == previous
    # Explicit target is never silently exchanged for host even in auto.
    foreign = 'linux-aarch64-gnu' if os.name == 'nt' or os.uname().machine != 'aarch64' else 'windows-x86_64-mingw64'
    build(p, 'auto', foreign, str(root / 'absent'), success=False)
    assert p[1].read_bytes() == previous

    # An imported library's native part does not satisfy own required mode.
    dep = project('dependency', 'external f=func(x){return x+1;}; f(3);', {'mode': 'required'})
    build(dep)
    dep_copy = root / 'dependency@1.gbin'
    shutil.copyfile(dep[1], dep_copy)
    app = project('only-dependency', 'println((import "dependency@1").f(2));', {'mode': 'required'})
    directory = app[0].parent / 'modules' / 'dependency'
    directory.mkdir(parents=True)
    shutil.copyfile(dep[1], directory / '1.gbin')
    run = build(app, success=False)
    assert not run.stdout and b'own specialization' in run.stderr, run.stderr

print(f'{passed} module native build scenarios passed')
