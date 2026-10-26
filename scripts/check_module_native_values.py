#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Build real module-v1 adapters and execute them in a same-bitness C process."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

builder = str(Path(sys.argv[1]).resolve())
runner = str(Path(sys.argv[2]).resolve())
compiler = shutil.which(sys.argv[3]) or sys.argv[3]
with tempfile.TemporaryDirectory(prefix='goat-native-values-') as temporary:
    directory = Path(temporary)
    source = '''external square=func(x){return x*x;}; square(7);
external real=func(x){return x;}; real(1.5);
external boolean=func(x){return x;}; boolean(true);
external character=func(x){return x;}; character('a');
external text=func(x){return x;}; text("observed");
external upper=func(x){return x.toUpperCase();}; upper("observed");
external trim=func(x){return x.trim();}; trim(" observed ");
'''
    (directory / 'main.goat').write_text(source, encoding='utf-8')
    descriptor = directory / 'module.gmod'
    descriptor.write_text(json.dumps(dict(formatVersion=1, name='values', version='1',
                                         main='main.goat', native=dict(mode='required'))))
    archive = directory / 'module.gbin'
    run = subprocess.run([builder, 'native-build', str(descriptor), str(archive),
                          'descriptor', 'descriptor', compiler], capture_output=True, timeout=120)
    assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
    assert not run.stdout, run.stdout
    with zipfile.ZipFile(archive) as zip_file:
        metadata = zip_file.read('module.json')
        doc = json.loads(metadata)
        native = doc['native'][0]
        library = directory / ('values.dll' if os.name == 'nt' else 'values.so')
        library.write_bytes(zip_file.read(native['library']))
    manifest = directory / 'module.json'
    manifest.write_bytes(metadata)
    run = subprocess.run([runner, str(library), str(manifest)], capture_output=True, timeout=30)
    assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
    print(run.stdout.decode().strip())

    archive = directory / 'unsafe-character.gbin'
    # The language preserves arbitrary uint32 characters, unlike module ABI v1.
    # Unsafe chr construction and its native callers stay on the VM path.
    (directory / 'main.goat').write_text('external convert=func(x){return chr(x);}; external indirect=func(x){return convert(x);}; external square=func(x){return x*x;}; convert(-1); indirect(-1); square(7); println(ord(convert(-1)));', encoding='utf-8')
    run = subprocess.run([builder, 'native-build', str(descriptor), str(archive),
                          'descriptor', 'descriptor', compiler], capture_output=True, timeout=120)
    assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
    with zipfile.ZipFile(archive) as zip_file:
        doc = json.loads(zip_file.read('module.json'))
    ids = {x['functionId'] for x in doc['native'][0]['specializations']}
    assert doc['externals']['square']['functionId'] in ids
    assert doc['externals']['convert']['functionId'] not in ids
    assert doc['externals']['indirect']['functionId'] not in ids
    run = subprocess.run([builder, 'binary', str(archive)], capture_output=True, timeout=30)
    assert run.returncode == 0 and run.stdout.strip() == b'4294967295', (run.stdout, run.stderr)
    (directory / 'main.goat').write_text('external convert=func(x){return chr(x);}; convert(-1); println("must not run");', encoding='utf-8')
    run = subprocess.run([builder, 'native-build', str(descriptor), str(archive),
                          'descriptor', 'descriptor', compiler], capture_output=True, timeout=120)
    assert run.returncode != 0 and not run.stdout and b'own specialization' in run.stderr, (run.stdout, run.stderr)
    print('Unsafe Unicode construction remains VM-only; required rejects before execution')
