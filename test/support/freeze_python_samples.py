#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Explicit, independent reproduction/check of frozen pre-migration inputs.

No production executable or expected-result oracle is imported or run.
--write is the only mode that writes files. Normal consumers only read fixtures.
"""
import argparse
import hashlib
import json
import tempfile
from pathlib import Path

import python_samples_binary as binary
import python_samples_structured as structured

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'test/data/python-samples'
BASELINE = '89b8d80ba13dfa3cb5ca9d95b1448d45fec438f2'


def encode(value):
    if isinstance(value, bytes):
        return value
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def generated():
    values = {**binary.samples(), **structured.samples()}
    metadata = {**binary.METADATA, **structured.METADATA}
    sources = {**binary.SOURCE_SHA256, **structured.SOURCE_SHA256}
    assert values.keys() == metadata.keys()
    files = {name: encode(value) for name, value in values.items()}
    manifest = dict(version=1, baseline_commit=BASELINE, sources_sha256=sources,
                    samples=[dict(path=name, **metadata[name], bytes=len(data),
                                  sha256=hashlib.sha256(data).hexdigest())
                             for name, data in sorted(files.items())])
    # Also pin each fully materialized damaged archive, not just its mutation recipe.
    for prefix, base_name in [('zip', 'zip-damage-base.bin'),
                              ('zip-windows', 'zip-damage-base-windows.bin'),
                              ('container', 'container-damage-base.bin')]:
        hashes = []
        for mutations in values[('zip' if prefix == 'zip-windows' else prefix) + '-damage.json']:
            damaged = bytearray(values[base_name])
            assert len(mutations) == 3
            for offset, mask in mutations:
                assert 0 <= offset < len(damaged) and mask in (1, 2, 4, 8, 16, 32, 64, 128)
                damaged[offset] ^= mask
            hashes.append(hashlib.sha256(damaged).hexdigest())
        manifest[prefix + '_damaged_inputs_sha256'] = hashes
    files['manifest.json'] = encode(manifest)
    return files


def check(directory, files):
    errors = []
    for name, expected in files.items():
        path = directory / name
        if not path.is_file() or path.read_bytes() != expected:
            errors.append(name)
    if errors:
        raise ValueError('Frozen samples differ: ' + ', '.join(errors))


def self_test(files):
    # Exercise rejected corruption/missing files away from the read-only corpus.
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='frozen-samples-', dir=build) as tmp:
        directory = Path(tmp)
        for name, data in files.items():
            (directory / name).write_bytes(data)
        check(directory, files)
        for name in files:
            path = directory / name
            for replacement in (b'corrupt', None):
                if replacement is None:
                    path.unlink()
                else:
                    path.write_bytes(replacement)
                try:
                    check(directory, files)
                except ValueError:
                    pass
                else:
                    raise AssertionError('accepted damaged fixture: ' + name)
                assert path.read_bytes() == replacement if replacement is not None else not path.exists()
                path.write_bytes(files[name])
    print('Corrupted and missing fixtures rejected without repairs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true', help='compare only; never write')
    mode.add_argument('--self-test', action='store_true', help='negative checks in an isolated build directory')
    mode.add_argument('--write', action='store_true', help='explicitly replace committed inputs')
    parser.add_argument('--output', type=Path, default=DATA,
                        help='output/check directory (default: committed fixtures)')
    args = parser.parse_args()
    files = generated()
    if args.self_test:
        self_test(files)
        return
    if args.write:
        args.output.mkdir(parents=True, exist_ok=True)
        for name, data in files.items():
            (args.output / name).write_bytes(data)
    check(args.output, files)
    print('Frozen Python inputs verified: %d fixtures, 6 original sources' % (len(files) - 1))


if __name__ == '__main__':
    main()
