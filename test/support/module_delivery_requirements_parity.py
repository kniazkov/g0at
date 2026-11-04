#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Temporary #389 original/C gate. Remove only after all 14 remote checks pass.

The original is imported byte-for-byte and all its assertions execute. Hooks
only retain its workspace and verify generated inputs against frozen fixtures.
No compiler, ELF, selector or execution result is substituted.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def fixtures(data):
    directory = data / 'module-delivery-requirements'
    manifest = json.loads((directory / 'manifest.json').read_text())
    require(hashlib.sha256((ROOT / manifest['original']).read_bytes().replace(b'\r\n', b'\n')).hexdigest()
            == manifest['originalSha256'], 'original checker changed')
    for name, digest in manifest['files'].items():
        require(hashlib.sha256((directory / name).read_bytes()).hexdigest() == digest,
                'frozen input changed: ' + name)
    return directory


def original(args, mode, destination, capture):
    spec = importlib.util.spec_from_file_location('original_requirements', ROOT / 'scripts/check_module_delivery_requirements.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    frozen = fixtures(args.data)
    retained = []
    original_temp = tempfile.TemporaryDirectory
    original_write = Path.write_text
    writes = []

    class Retained:
        def __init__(self, *unused, **kwargs):
            self.name = str(capture / ('original-work-' + str(len(retained))))
            Path(self.name).mkdir()
            retained.append(Path(self.name))
        def __enter__(self):
            return self.name
        def __exit__(self, *unused):
            return False

    def checked_write(path, text, *positional, **kwargs):
        name = {'main.goat': 'main.goat', 'module.gmod': 'floor.gmod' if mode == 'prepare-floor' else 'audit.gmod',
                'runtime_floor.c': 'runtime_floor.c.txt'}.get(path.name)
        require(name is not None, 'unfrozen original text write: ' + str(path))
        require(text.encode('utf-8') == (frozen / name).read_bytes(), 'original input differs: ' + name)
        writes.append(name)
        return original_write(path, text, *positional, **kwargs)

    output = io.StringIO()
    try:
        tempfile.TemporaryDirectory = Retained
        Path.write_text = checked_write
        with contextlib.redirect_stdout(output):
            if mode == 'audit':
                module.audit(str(args.goat), str(args.wrapper), str(args.selector), args.cc)
            elif mode == 'prepare-floor':
                module.prepare(str(args.goat), str(args.wrapper), args.cc, destination)
            elif mode == 'verify-floor':
                module.verify(str(args.goat), destination)
            else:
                print(module.floor_image(destination))
    finally:
        tempfile.TemporaryDirectory = original_temp
        Path.write_text = original_write
        (capture / ('original-' + mode + '.stdout')).write_text(output.getvalue(), encoding='utf-8')
    if mode == 'audit':
        require(writes == ['main.goat', 'audit.gmod'], 'original audit source coverage changed')
        destination.mkdir()
        for name, source in [('baseline.gbin', 'modules/requirements/1.gbin'), ('extended.gbin', 'extended/requirements/1.gbin')]:
            path = retained[0] / source
            if path.exists():
                (destination / name).write_bytes(path.read_bytes())
    elif mode == 'prepare-floor':
        require(writes == ['main.goat', 'floor.gmod', 'runtime_floor.c.txt'], 'original floor input coverage changed')
    return output.getvalue()


def migrated(args, mode, destination, capture):
    command = [str(args.runner), 'integration.module_delivery_requirements.' + mode,
               str(args.goat), str(args.wrapper), str(args.selector), args.cc,
               str(args.build), str(args.data), str(args.format_root), '--destination', str(destination)]
    result = subprocess.run(command, capture_output=True, timeout=1000)
    (capture / ('c-' + mode + '.stdout')).write_bytes(result.stdout)
    (capture / ('c-' + mode + '.stderr')).write_bytes(result.stderr)
    require(result.returncode == 0, f'C {mode} failed ({result.returncode}): {result.stderr.decode(errors="replace")}')
    return result.stdout.decode('utf-8')


def archive_observation(path):
    with zipfile.ZipFile(path) as archive:
        info = archive.infolist()
        require(len({part.filename for part in info}) == len(info), 'duplicate archive member: ' + str(path))
        members = {part.filename: archive.read(part) for part in info}
    metadata = json.loads(members['module.json'])
    native_paths = set()
    for variant in metadata.get('native', []):
        name = variant['library']
        require(name not in native_paths, 'duplicate native variant library')
        native_paths.add(name)
        body = members[name]
        require(body and hashlib.sha256(body).hexdigest() == variant.pop('hash'),
                'native hash does not match actual untouched payload: ' + str(path))
    portable = {name: raw for name, raw in members.items()
                if name != 'module.json' and name not in native_paths}
    return {'metadata': metadata, 'portable': portable,
            'members': sorted((part.filename, part.compress_type) for part in info)}


def same_artifact(left, right):
    if os.name == 'nt':
        # Independent PE links carry producer timestamps (as in the #388 gate).
        # Validate each full payload hash, compare all remaining metadata and
        # portable bytes, then replay untouched ORIGINAL archives through C.
        require(archive_observation(left) == archive_observation(right),
                f'Windows archive observations differ: {left}, {right}')
        return
    # Linux determinism reproduced with two independent original producer runs.
    # Every actual member, including full ELF bytes and hashes, must match.
    with zipfile.ZipFile(left) as a, zipfile.ZipFile(right) as b:
        require(len(set(a.namelist())) == len(a.namelist()), 'duplicate original ZIP member')
        require(len(set(b.namelist())) == len(b.namelist()), 'duplicate C ZIP member')
        require(sorted(a.namelist()) == sorted(b.namelist()), f'archive members differ: {left}, {right}')
        for name in a.namelist():
            require(a.read(name) == b.read(name), f'archive member differs: {name}: {left}, {right}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['audit', 'prepare-floor', 'floor-image', 'verify-floor', 'fixtures'])
    for name in ('runner', 'goat', 'wrapper', 'selector', 'build', 'data', 'format-root'):
        parser.add_argument('--' + name, type=lambda s: Path(s).resolve(), required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--destination', type=lambda s: Path(s).resolve())
    args = parser.parse_args()
    fixtures(args.data)
    if args.mode == 'fixtures':
        print('Runtime requirements frozen inputs verified')
        return
    args.build.mkdir(parents=True, exist_ok=True)
    capture = Path(tempfile.mkdtemp(prefix='requirements-parity-', dir=args.build))
    try:
        if args.mode in ('audit', 'prepare-floor'):
            root = args.destination or capture / 'artifacts'
            root.mkdir(parents=True, exist_ok=False)
            old, new = root / 'original', root / 'c'
            expected = original(args, args.mode, old, capture)
            actual = migrated(args, args.mode, new, capture)
            require(expected.strip() in actual, 'original/C phase summary differs')
            require(sorted(p.name for p in old.iterdir()) == sorted(p.name for p in new.iterdir()), 'artifact set differs')
            for path in old.iterdir():
                same_artifact(path, new / path.name)
            if args.mode == 'audit':
                replay = migrated(args, 'replay-audit', old, capture)
                counts = re.findall(r'(\d+) real-archive selection checks', expected)
                require(len(counts) == 1 and f'{counts[0]} real-archive selection checks' in replay,
                        'original/C identical-artifact selection coverage differs')
            print(f'{args.mode}: original/C artifact parity passed; artifacts {root}')
        else:
            require(args.destination is not None, '--destination is required')
            images = []
            for index, directory in enumerate((args.destination / 'original', args.destination / 'c')):
                phase = capture / str(index)
                phase.mkdir()
                expected = original(args, args.mode, directory, phase)
                actual = migrated(args, args.mode, directory, phase)
                require(expected.strip() in actual, 'original/C runtime summary differs')
                images.append(expected.strip())
            require(images[0] == images[1], 'original/C artifact runtime differs')
            if args.mode == 'floor-image':
                print(images[0])
            else:
                print('Exact-runtime original/C verification passed on BOTH original and C artifacts')
    except BaseException:
        print('Runtime requirements parity failed; retained evidence: ' + str(capture), file=__import__('sys').stderr)
        raise


if __name__ == '__main__':
    main()
