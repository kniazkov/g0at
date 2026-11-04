#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Temporary #390 gate: independent production, cross-consumption, immutable bytes."""
import hashlib
import json
import os
import ast
import re
import shutil
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


action, build, family, destination = sys.argv[1:]
require(action in ('produce', 'verify'), 'invalid phase')
require(family in ('glibc', 'musl'), 'invalid libc')
fixtures = ROOT / 'test/data/module-musl'
manifest = json.loads((fixtures / 'manifest.json').read_text())
original = (ROOT / manifest['source']).read_bytes().replace(b'\r\n', b'\n')
require(hashlib.sha256(original).hexdigest() == manifest['sourceSha256'], 'original checker changed')
for name, digest in manifest['inputs'].items():
    require(hashlib.sha256((fixtures / name).read_bytes()).hexdigest() == digest, 'fixture changed: ' + name)
# Confirm provenance against actual original AST literals without executing or modifying it.
literals = {node.value for node in ast.walk(ast.parse(original))
            if isinstance(node, ast.Constant) and isinstance(node.value, str)}
require((fixtures / 'main.goat').read_text() in literals, 'source not in original producer')
descriptors = []
for node in ast.walk(ast.parse(original)):
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'dict':
        try:
            descriptors.append({kw.arg: ast.literal_eval(kw.value) for kw in node.keywords})
        except (ValueError, TypeError):
            pass
require(json.loads((fixtures / 'module.gmod').read_text()) in descriptors, 'descriptor provenance')
build, destination = Path(build).resolve(), Path(destination).resolve()
destination.mkdir(parents=True, exist_ok=True)


def invoke(implementation, phase, pair):
    if implementation == 'original':
        command = [sys.executable, ROOT / 'scripts/check_module_musl.py', phase,
                   build / 'goat', build / 'module_native_testing', family, pair]
    else:
        command = [build / 'module_musl_scenarios', 'integration.module_musl.' + phase,
                   build / 'goat', build / 'goat-inspect', build / 'goat-merge',
                   build / 'module_native_testing', os.environ.get('CC', 'gcc'), family, build,
                   ROOT / 'test/data', ROOT / 'docs/module-format-v1', '--destination', pair]
    log = destination / ('parity-' + family + '-' + build.name + '-' + phase + '-' +
                         pair.name + '-' + implementation)
    log.with_suffix('.command.json').write_text(json.dumps(list(map(str, command))))
    try:
        result = subprocess.run(list(map(str, command)), capture_output=True, timeout=2400)
    except subprocess.TimeoutExpired as error:
        log.with_suffix('.stdout').write_bytes(error.stdout or b'')
        log.with_suffix('.stderr').write_bytes((error.stderr or b'') + b'\nPARITY PROCESS TIMEOUT\n')
        raise RuntimeError(('parity process timeout', command)) from error
    log.with_suffix('.stdout').write_bytes(result.stdout)
    log.with_suffix('.stderr').write_bytes(result.stderr)
    log.with_suffix('.command.json').write_text(json.dumps(list(map(str, command))))
    if result.returncode:
        # Preserve only this runner's reported retained workspace, including
        # intermediate archives and reports before destination persistence.
        for retained in re.findall(r'retained work: ([^\r\n]+)', result.stderr.decode('utf-8', 'replace')):
            work = Path(retained).resolve()
            if work.parent == build and work.name.startswith('scenario-') and work.is_dir():
                evidence = destination / 'failed-workspaces' / (family + '-' + build.name) / work.name
                shutil.copytree(work, evidence, dirs_exist_ok=True)
        sys.stderr.buffer.write(result.stdout + result.stderr)
        raise RuntimeError((command, result.returncode))
    return result.stdout


def snapshot(pair):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(pair.iterdir()) if p.suffix in ('.gbin', '.json')}


def assert_same_artifact(a, b):
    # ZIP container timestamps/order are not portable identity. Every member,
    # including the full native ELF bytes, must match. No result substitution.
    with zipfile.ZipFile(a) as left, zipfile.ZipFile(b) as right:
        require(sorted(left.namelist()) == sorted(right.namelist()), 'archive member names')
        for name in left.namelist():
            x, y = left.read(name), right.read(name)
            if name == 'module.json':
                require(json.loads(x) == json.loads(y), name)
            else:
                require(x == y, name)


if action == 'produce':
    outputs = []
    for implementation in ('original', 'migrated'):
        pair = destination / implementation
        pair.mkdir(exist_ok=True)
        outputs.append(invoke(implementation, 'produce', pair))
    require(outputs[0] == outputs[1], outputs)
    assert_same_artifact(destination / 'original' / (family + '.gbin'),
                         destination / 'migrated' / (family + '.gbin'))
    require(json.loads((destination / 'original' / (family + '-host.json')).read_text()) == json.loads(
        (destination / 'migrated' / (family + '-host.json')).read_text()), 'host records differ')
    for implementation in ('original', 'migrated'):
        pair = destination / implementation
        paths = [pair / (family + '.gbin'), pair / (family + '-host.json')]
        (destination / (implementation + '-' + family + '-sealed.json')).write_text(json.dumps(
            {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}, sort_keys=True))
elif action == 'verify':
    for producer in ('original', 'migrated'):
        pair = destination / producer
        expected = {}
        for libc in ('glibc', 'musl'):
            expected.update(json.loads((destination / (producer + '-' + libc + '-sealed.json')).read_text()))
        require(snapshot(pair) == expected, 'sealed artifacts changed')
        outputs = []
        for consumer in ('original', 'migrated'):
            outputs.append(invoke(consumer, 'verify', pair))
            require(snapshot(pair) == expected, 'sealed artifacts changed')
        require(outputs[0] == outputs[1], outputs)
else:
    raise ValueError(action)
print('Original/C real-libc ' + action + ' parity: ' + family)
