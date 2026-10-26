#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Real glibc/musl producer and compiler-free consumer acceptance.

Run produce in each libc userspace, then verify in BOTH userspaces against the
same pair of untouched archives. No native bytes or runtime labels are patched.
The ordinary delivery/package suites cover the full module graph separately.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref


def run(command, fail=False, **kwargs):
    if 'input' not in kwargs:
        kwargs['stdin'] = subprocess.DEVNULL
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=180, **kwargs)
    assert (result.returncode != 0) == fail, (command, result.returncode, result.stdout, result.stderr)
    return result


def contents(path, family):
    with zipfile.ZipFile(path) as archive:
        files = {name: archive.read(name) for name in archive.namelist()}
    doc = json.loads(files['module.json'])
    assert doc['mainHash'] == ref.main_hash(doc, files['bytecode.bin'])
    assert len(doc['native']) == 1, doc
    variant = doc['native'][0]
    assert variant['goatAbiVersion'] == 1, variant
    target = variant['target']
    assert target['os'] == 'linux' and target['runtime']['family'] == family, target
    assert target['architecture'] == platform.machine() and target['bits'] == 64, target
    assert target['cpuFeatures'] == {'x86_64': ['sse2'], 'aarch64': ['fp', 'neon']}[platform.machine()]
    assert hashlib.sha256(files[variant['library']]).hexdigest() == variant['hash']
    assert any(target['runtime']['minimumVersion']), target
    return doc, files


def produce(goat, selector, family, destination):
    destination.mkdir(parents=True, exist_ok=True)
    host = json.loads(run([selector, 'host']).stdout)
    assert host['runtime'] == {'glibc': 1, 'musl': 2}[family], host
    assert host['runtimeVersion']['known'], host
    with tempfile.TemporaryDirectory(prefix='goat-libc-producer-') as temporary:
        source = Path(temporary)
        (source / 'main.goat').write_text(
            'external square=func(x){return x*x;}; var n=int(input()); println(square(n));', encoding='utf-8')
        descriptor = source / 'module.gmod'
        descriptor.write_text(json.dumps(dict(formatVersion=1, name='libc-delivery',
                                              version='1', main='main.goat')), encoding='utf-8')
        target = 'linux-' + platform.machine() + ('-musl' if family == 'musl' else '-gnu')
        compiler = os.environ.get('CC', 'gcc')
        listed = run([goat, '--list-native-targets', '--cc', compiler]).stdout
        assert target.encode() in listed, listed
        run([goat, 'build', descriptor, '--native', 'required', '--cc', compiler,
             '--native-target', target])
        archive = source / 'modules/libc-delivery/1.gbin'
        doc, files = contents(archive, family)
        minimum = doc['native'][0]['target']['runtime']['minimumVersion']
        actual = host['runtimeVersion']['components']
        padded = (minimum + [0] * 4)[:4]
        assert padded <= actual, (minimum, actual)
        if family == 'musl':
            # musl has no symbol-version floor: conservatively require the
            # actual linked libc version, and run at that exact version here.
            assert padded == actual, (minimum, actual)
        shutil.copyfile(archive, destination / (family + '.gbin'))
        (destination / (family + '-host.json')).write_text(json.dumps(host), encoding='utf-8')
    print('Produced real', family, target, 'minimum', minimum, flush=True)


def verify(goat, selector, family, destination):
    own = destination / (family + '.gbin')
    foreign_family = 'glibc' if family == 'musl' else 'musl'
    foreign = destination / (foreign_family + '.gbin')
    own_doc, own_files = contents(own, family)
    foreign_doc, foreign_files = contents(foreign, foreign_family)
    assert own_doc['mainHash'] == foreign_doc['mainHash']
    assert own_files['bytecode.bin'] == foreign_files['bytecode.bin']
    host = json.loads(run([selector, 'host']).stdout)
    assert host == json.loads((destination / (family + '-host.json')).read_text()), host
    inspector = goat.with_name('goat-inspect')
    merger = goat.with_name('goat-merge')
    with tempfile.TemporaryDirectory(prefix='goat-libc-consumer-') as temporary:
        root = Path(temporary)
        own_copy, foreign_copy, mixed = root / 'own.gbin', root / 'foreign.gbin', root / 'mixed.gbin'
        shutil.copyfile(own, own_copy)
        shutil.copyfile(foreign, foreign_copy)
        run([merger, mixed, own_copy, foreign_copy])
        with zipfile.ZipFile(mixed) as archive:
            merged = json.loads(archive.read('module.json'))
            assert merged['mainHash'] == own_doc['mainHash'] and len(merged['native']) == 2
            assert archive.read('bytecode.bin') == own_files['bytecode.bin']
        before = {p: p.read_bytes() for p in (own_copy, foreign_copy, mixed)}
        env = dict(os.environ, PATH=str(root / 'no-compiler'), CC=str(root / 'absent'),
                   CXX=str(root / 'absent'), GOAT_MODULE_PATH='')
        for archive in before:
            # Foreign runtime is compatible container data, not corruption.
            inspected = json.loads(run([inspector, '--json', archive], env=env, cwd=root).stdout)
            assert inspected['success'], inspected
            for mode in ('off', 'auto', 'required'):
                report = root / (archive.stem + '-' + mode + '.txt')
                fail = archive == foreign_copy and mode == 'required'
                result = run([goat, archive, '--native', mode, '--save-native', report,
                              '--cc', root / 'absent'], fail=fail, env=env, cwd=root, input=b'7\n')
                assert result.stdout == (b'' if fail else b'49\n'), result
                if fail:
                    assert result.stderr, result
                    continue
                fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
                native = mode != 'off' and archive != foreign_copy
                assert (int(fields['succeeded']) > 0) == native, fields
                if native:
                    assert int(fields['bound']) > 0, fields
                    # In particular, musl's small initial main stack mapping
                    # must not cause a silent guard fallback.
                    assert int(fields['succeeded']) == 1, fields
                else:
                    assert int(fields['bound']) == 0, fields
        assert all(path.read_bytes() == data for path, data in before.items())
        assert not list(root.glob('*.goat')) and not list(root.glob('*.gmod'))
    print(f'{family}: own/mixed real native calls; foreign VM fallback/required rejection; '
          'all three modes; identical portable identity; immutable archives', flush=True)


if __name__ == '__main__':
    action, goat, selector, family, destination = sys.argv[1:]
    assert family in ('glibc', 'musl')
    {'produce': produce, 'verify': verify}[action](Path(goat).resolve(), Path(selector).resolve(),
                                                 family, Path(destination).resolve())
