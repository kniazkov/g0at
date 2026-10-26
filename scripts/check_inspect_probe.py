#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Real isolated native inspection: ABI, capture, deadlines and process ownership.

Run with goat-inspect, module_import_execution_testing, a host C compiler,
and inspect_probe_testing; goat-merge must be beside goat-inspect. Every subprocess
has an independent outer deadline; marker barriers, not elapsed sleeps, prove races.
"""
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

from process_test_support import ProcessWatch, until
from inspect_report_test_support import schema_check

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
BUILDER = str(Path(sys.argv[2]).resolve())
CC = shutil.which(sys.argv[3]) or sys.argv[3]
HOST = str(Path(sys.argv[4]).resolve())
MERGE = str(Path(EXE).with_name("goat-merge" + Path(EXE).suffix))
COUNT = 0
SCHEMA_PATH = ROOT / "docs/module-format-v1/inspect-report.schema.json"
SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


def checked(name):
    global COUNT
    COUNT += 1
    print('[ ok ]', name, flush=True)


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, timeout=kwargs.pop('timeout', 60), **kwargs)


def read_archive(path):
    with zipfile.ZipFile(path) as archive:
        files = {name: archive.read(name) for name in archive.namelist()}
    return json.loads(files.pop('module.json')), files


def save_archive(path, doc, files):
    doc = copy.deepcopy(doc)
    for variant in doc.get('native', []):
        variant['hash'] = hashlib.sha256(files[variant['library']]).hexdigest()
    doc['mainHash'] = ref.main_hash(doc, files['bytecode.bin'])
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('module.json', ref.canonical(doc))
        for name in sorted({'bytecode.bin'} | {v['library'] for v in doc.get('native', [])}):
            archive.writestr(name, files[name])
    return path


def build(root, name, dependencies=(), source=None):
    directory = root / name
    directory.mkdir()
    for dep in dependencies:
        doc, _ = read_archive(dep)
        destination = directory / 'modules' / doc['name'] / (doc['version'] + '.gbin')
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(dep, destination)
    (directory / 'main.goat').write_text(source or
        'external square=func(n){return n*n;}; println(square(7));', encoding='utf-8')
    descriptor = directory / 'module.gmod'
    descriptor.write_text(json.dumps(dict(formatVersion=1, name=name, version='1',
        main='main.goat', native=dict(mode='required'))), encoding='utf-8')
    output = directory / 'module.gbin'
    result = run([BUILDER, 'native-build', str(descriptor), str(output),
                  'descriptor', 'descriptor', CC], timeout=120)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    return output


def compile_c(source, output, shared=True):
    command = [CC, '-std=c11', '-O0', '-I', str(ROOT / 'include'), str(source), '-o', str(output)]
    if shared:
        command.insert(1, '-shared')
        if os.name != 'nt':
            command.insert(1, '-fPIC')
    result = run(command)
    assert result.returncode == 0, (command, result.stdout, result.stderr)


def custom(root, original, kind='valid'):
    doc, files = read_archive(original)
    variant = doc['native'][0]
    description = dict(formatVersion=1, goatAbiVersion=1, module=doc['name'],
        version=doc['version'], mainHash=doc['mainHash'], target=variant['target'],
        specializations=variant['specializations'])
    if kind == 'descriptor-mismatch':
        description['mainHash'] = '0' * 64
    encoded = ref.canonical(description)
    blob = b'GOATND01' + struct.pack('<Q', len(encoded)) + encoded + b'\0'
    if kind == 'descriptor-overflow':
        blob = b'GOATND01' + b'\xff' * 8 + b'\0'
    parts = ['#include "goat/module_abi.h"',
             '#include "' + str(ROOT / 'src/component_test/inspect_probe_library.c').replace('\\', '/') + '"']
    if kind != 'descriptor-missing':
        parts.append('GOAT_MODULE_EXPORT const unsigned char goat_module_descriptor_v1[]={'
                     + ','.join(map(str, blob)) + '};')
    for index, spec in enumerate(variant['specializations']):
        if kind == 'symbol-missing' and index == len(variant['specializations']) - 1:
            continue
        parts.append('GOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL ' + spec['symbol'] +
            '(uint32_t abi,const goat_module_context_v1*c,uint32_t n,'
            'const goat_module_value_v1*a,goat_module_value_v1*r){'
            '(void)abi;(void)c;(void)n;(void)a;(void)r;mark("called");abort();return 0;}')
    source = root / (kind + '-' + doc['name'] + '.c')
    source.write_text('\n'.join(parts), encoding='utf-8')
    library = source.with_suffix('.dll' if os.name == 'nt' else '.so')
    compile_c(source, library)
    files[variant['library']] = library.read_bytes()
    return save_archive(source.with_suffix('.gbin'), doc, files)


def ready_file(path):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return ''


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, ('duplicate JSON key', key)
        result[key] = value
    return result


class Case:
    def __init__(self, root, name, worker):
        self.root = root / ('case-' + name)
        self.root.mkdir()
        self.marker = self.root / 'markers'
        self.marker.mkdir()
        self.watches = []
        self.exe = EXE
        self.env = dict(os.environ, TMPDIR=str(self.root), TMP=str(self.root), TEMP=str(self.root),
            GOAT_PROBE_TEST_MARKERS=str(self.marker), GOAT_PROBE_TEST_WORKER=str(worker))

    def command(self, archive, *options):
        return [self.exe, '--json', *options, str(archive)]

    def report(self, archive, mode='valid', expected=0, *options):
        self.env['GOAT_PROBE_TEST_MODE'] = mode
        started = time.monotonic()
        if mode.startswith('tree'):
            owner = subprocess.Popen(self.command(archive, *options), env=self.env,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                until(lambda: ready_file(self.marker / 'descendant') or owner.poll() is not None, 10)
                assert (self.marker / 'descendant').exists(), owner.communicate(timeout=10)
                self.watches.append(ProcessWatch(int((self.marker / 'descendant').read_text())))
                (self.marker / 'descendant-pinned').touch()
                stdout, stderr = owner.communicate(b'parent input must not leak', timeout=25)
                result = subprocess.CompletedProcess(owner.args, owner.returncode, stdout, stderr)
            finally:
                if owner.poll() is None:
                    owner.kill()
                    owner.communicate(timeout=10)
        else:
            result = run(self.command(archive, *options), env=self.env,
                         input=b'parent input must not leak', timeout=25)
        elapsed = time.monotonic() - started
        if result.returncode != expected:
            for watch in self.watches:
                if watch.alive():
                    watch.kill()
                    watch.wait(10)
                watch.close()
            self.watches = []
        assert result.returncode == expected, (mode, result.returncode, result.stdout, result.stderr)
        report = json.loads(result.stdout, object_pairs_hook=unique_object)
        schema_check(report, SCHEMA, SCHEMA, SCHEMA_PATH.parent)
        assert report['exitCode'] == expected and report['success'] == (expected == 0), report
        assert report['reportVersion'] == 1
        assert not result.stderr, result.stderr
        return report, elapsed

    def loading(self, archive, mode='valid', expected=0, *options):
        report, elapsed = self.report(archive, mode, expected, *options)
        nodes = [v['loading'] for m in report['modules'] for v in m['native']
                 if v['compatibility']['state'] == 'compatible']
        assert len(nodes) == 1, report
        loading = nodes[0]
        assert loading['state'] == 'checked', report
        assert loading['cleanup']['state'] == 'passed', report
        assert loading['cleanup']['remainingPaths'] == [], report
        assert not list(self.root.glob('goat-run-*')), list(self.root.iterdir())
        return loading['probe'], elapsed

    def events(self):
        file = self.marker / 'events'
        return file.read_text() if file.exists() else ''

    def descendants(self):
        assert self.watches, 'Descendant identity was not pinned before completion'
        for watch in self.watches:
            try:
                watch.wait(10)
            finally:
                watch.close()
        self.watches = []



with tempfile.TemporaryDirectory(prefix='goat-inspect-probe-') as temporary:
    root = Path(temporary)
    worker = root / ('worker.exe' if os.name == 'nt' else 'worker')
    compile_c(ROOT / 'src/component_test/inspect_probe_worker.c', worker, shared=False)
    original = build(root, 'probe')
    valid = custom(root, original)

    case = Case(root, 'success', worker)
    probe, elapsed = case.loading(valid)
    assert probe['state'] == 'passed' and probe['stage'] == 'exit', probe
    assert probe['completedStages'] == ['load', 'descriptor', 'symbols', 'unload'], probe
    assert case.events() == 'loaded\nunloaded\n', case.events()
    assert elapsed < 5, ('Fast probe artificially waited for its default deadline', elapsed)
    checked('actual load, ABI/symbol validation, explicit unload; no user function or bytecode execution')

    for size in (0, 65536, 65537, 262144):
        case = Case(root, 'capture-' + str(size), worker)
        case.env['GOAT_PROBE_TEST_BYTES'] = str(size)
        probe, _ = case.loading(valid, 'output')
        assert probe['state'] == 'passed', probe
        for stream, offset in (('stdout', 0), ('stderr', 97)):
            capture = probe['capture'][stream]
            data = base64.b64decode(capture['dataBase64'], validate=True)
            assert data == bytes((i + offset) % 256 for i in range(min(size, 65536)))
            assert capture['truncated'] == (size > 65536), capture
        checked('binary stdout/stderr exact capture boundary ' + str(size))

    case = Case(root, 'stdin', worker)
    case.env['GOAT_PROBE_TEST_MODE'] = 'stdin'
    owner = subprocess.Popen(case.command(valid), env=case.env, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        # Do not close the parent's stdin: child EOF must not depend on it.
        owner.wait(timeout=15)
        stdout, stderr = owner.communicate(b'parent input')
        report = json.loads(stdout)
        assert owner.returncode == 0 and report['success'] and not stderr, (stdout, stderr)
        assert (case.marker / 'eof').exists() and 'called' not in case.events()
    finally:
        if owner.poll() is None:
            owner.kill()
            owner.communicate(timeout=10)
    checked('stdin EOF from child startup while parent stdin remains open')

    for mode, state, stage in [('load-crash', 'crashed', 'load'),
                               ('load-hang', 'timeout', 'load'),
                               ('output-hang', 'timeout', 'load'),
                               ('unload-crash', 'crashed', 'unload'),
                               ('unload-hang', 'timeout', 'unload'),
                               ('load-exit-zero', 'failed', 'load')]:
        case = Case(root, mode, worker)
        probe, elapsed = case.loading(valid, mode, 0, '--probe-timeout', '1')
        assert probe['state'] == state and probe['stage'] == stage, probe
        assert probe['diagnostics'], probe
        assert case.events().splitlines().count('loaded') == 1 and 'called' not in case.events(), case.events()
        assert elapsed < 12, elapsed
        if mode == 'output-hang':
            assert all(probe['capture'][stream]['truncated'] for stream in ('stdout', 'stderr')), probe
            assert elapsed < 5, ('Output activity extended the fixed deadline', elapsed)
        checked(mode + ': warning, bounded, no retry')

    case = Case(root, 'default-timeout', worker)
    probe, elapsed = case.loading(valid, 'load-hang')
    assert probe['state'] == 'timeout' and probe['stage'] == 'load', probe
    assert 4.5 <= elapsed < 12, ('Default five-second monotonic deadline', elapsed)
    checked('default five-second timeout applies without an explicit CLI override')

    for kind, stage in [('descriptor-missing', 'descriptor'), ('descriptor-mismatch', 'descriptor'),
                        ('descriptor-overflow', 'descriptor'), ('symbol-missing', 'symbols')]:
        case = Case(root, kind, worker)
        probe, _ = case.loading(custom(root, original, kind), 'valid', 1)
        assert probe['state'] == 'failed' and probe['stage'] == stage, probe
        assert probe['diagnostics'], probe
        assert case.events() == 'loaded\nunloaded\n', case.events()
        checked(kind + ': ABI violation is an error, not a load warning')

    doc, files = read_archive(original)
    files[doc['native'][0]['library']] = b'not a native image\0'
    invalid = save_archive(root / 'bad-image.gbin', doc, files)
    probe, _ = Case(root, 'load-error', worker).loading(invalid)
    assert probe['state'] == 'failed' and probe['stage'] == 'load', probe
    assert probe['diagnostics'], probe
    checked('OS loader failure is a warning')

    # Match the exact effective target, not merely the library's minimum requirements.
    case = Case(root, 'explicit-current', worker)
    current, _ = case.report(valid)
    target = current['target']
    profile = dict(os=target['os'], architecture=target['architecture'], bits=target['bits'],
        endian='little' if target['littleEndian'] else 'big', minimumOs=target['osVersion'],
        cpuFeatures=target['cpuFeatures'],
        runtime=dict(family=target['runtime'], minimumVersion=target['runtimeVersion']))
    profile_file = root / 'current-target.json'
    profile_file.write_text(json.dumps(profile))
    case = Case(root, 'explicit-current-probe', worker)
    probe, _ = case.loading(valid, 'valid', 0, '--target', str(profile_file))
    assert probe['state'] == 'passed' and case.events() == 'loaded\nunloaded\n'
    profile['minimumOs'] = [999]
    profile_file.write_text(json.dumps(profile))
    case = Case(root, 'foreign', worker)
    report, _ = case.report(valid, 'valid', 0, '--target', str(profile_file))
    assert not case.events()
    assert all(n['loading']['state'] == 'not_checked' for m in report['modules'] for n in m['native'])
    checked('exact explicit current target probes; foreign profile is metadata-only')

    # A genuinely different OS/runtime must not reach this host-loadable payload.
    # Metadata compatibility is deliberate: an incompatible variant alone would
    # not prove that the explicit foreign-target gate prevents OS loading.
    opposite = copy.deepcopy(profile)
    opposite.update(os='linux' if os.name == 'nt' else 'windows', minimumOs=[10],
                    runtime=dict(family='glibc' if os.name == 'nt' else 'msvcrt',
                                 minimumVersion=[0]))
    opposite_file = root / 'opposite-os-target.json'
    opposite_file.write_text(json.dumps(opposite))
    foreign_doc, foreign_files = read_archive(valid)
    foreign_doc['native'][0]['target'] = opposite
    old_library = foreign_doc['native'][0]['library']
    foreign_library = 'native/opposite' + ('.so' if os.name == 'nt' else '.dll')
    foreign_files[foreign_library] = foreign_files.pop(old_library)
    foreign_doc['native'][0]['library'] = foreign_library
    opposite_archive = save_archive(root / 'opposite-os.gbin', foreign_doc, foreign_files)
    case = Case(root, 'opposite-os-compatible', worker)
    case.env.update(PATH='', CC='no-foreign-compiler', CXX='no-foreign-compiler')
    foreign, _ = case.report(opposite_archive, 'valid', 0, '--target', str(opposite_file))
    assert foreign['target']['os'] == opposite['os']
    variant = foreign['modules'][0]['native'][0]
    assert variant['compatibility']['state'] == 'compatible', foreign
    assert variant['loading']['state'] == 'not_checked' and not case.events(), foreign
    assert not list(case.root.glob('goat-run-*')), list(case.root.iterdir())
    checked('opposite OS/runtime metadata matches without loading host payload or finding a compiler')

    # Build a native diamond from validated metadata, avoiding runtime imports entirely.
    originals, _ = read_archive(original)
    _, template_files = read_archive(original)
    def node(name, dependencies=()):
        metadata = copy.deepcopy(originals)
        metadata['name'] = name
        if dependencies:
            metadata['dependencies'] = []
            for dependency in dependencies:
                dep, _ = read_archive(dependency)
                metadata['dependencies'].append(dict(name=dep['name'], version=dep['version'], hash=dep['mainHash']))
        raw = save_archive(root / (name + '-raw.gbin'), metadata, template_files)
        native = custom(root, raw)
        destination = root / 'modules' / name / '1.gbin'
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(native, destination)
        return destination
    leaf = node('diamond-leaf')
    left = node('diamond-left', (leaf,))
    right = node('diamond-right', (leaf,))
    graph = node('diamond-root', (left, right))
    case = Case(root, 'diamond', worker)
    report, _ = case.report(graph, 'valid', 0, '--module-dir', str(root / 'modules'))
    assert len(report['modules']) == 4, report
    assert case.events() == 'loaded\nunloaded\n' * 4, case.events()
    assert all(n['loading']['probe']['state'] == 'passed' for m in report['modules'] for n in m['native'])
    checked('one native probe per unique graph node, shared diamond dependency only once')
    selected_archives = [m['archive'] for m in report['modules']]
    leaf_bytes = leaf.read_bytes()
    case = Case(root, 'diamond-foreign', worker)
    foreign, _ = case.report(graph, 'valid', 0, '--module-dir', str(root / 'modules'),
                             '--target', str(profile_file))
    assert [m['archive'] for m in foreign['modules']] == selected_archives
    assert all(m['validation'] == 'passed' for m in foreign['modules'])
    assert all(n['loading']['state'] == 'not_checked' for m in foreign['modules']
               for n in m['native']), foreign
    assert not case.events(), 'A foreign target must never load any graph library'
    checked('foreign target preserves the selected diamond archives without native loading')

    # Search chooses an archive before considering its native variants. A later
    # copy with the same mainHash must not replace the first, even when only that
    # later copy contains a variant matching the requested foreign platform.
    neighboring_leaf = graph.parent / 'modules' / 'diamond-leaf' / '1.gbin'
    neighboring_leaf.parent.mkdir(parents=True)
    neighboring_leaf.write_bytes(leaf_bytes)
    later_doc, later_files = read_archive(leaf)
    later_doc['native'][0]['target'] = opposite
    old_library = later_doc['native'][0]['library']
    later_files[foreign_library] = later_files.pop(old_library)
    later_doc['native'][0]['library'] = foreign_library
    save_archive(leaf, later_doc, later_files)
    assert read_archive(leaf)[0]['mainHash'] == read_archive(neighboring_leaf)[0]['mainHash']
    selected = []
    for label, options in [('current', ()), ('foreign', ('--target', str(opposite_file)))]:
        case = Case(root, 'first-candidate-' + label, worker)
        result, _ = case.report(graph, 'valid', 0, '--module-dir', str(root / 'modules'), *options)
        selected.append([m['archive'] for m in result['modules']])
        shared = next(m for m in result['modules'] if m['metadata']['name'] == 'diamond-leaf')
        assert Path(shared['archive']) == neighboring_leaf.resolve(), shared
        assert shared['native'][0]['compatibility']['state'] == (
            'compatible' if label == 'current' else 'incompatible'), shared
        assert case.events() == ('loaded\nunloaded\n' * 4 if label == 'current' else '')
    assert selected[0] == selected[1], selected
    neighboring_leaf.unlink()
    case = Case(root, 'later-candidate-foreign', worker)
    case.env['GOAT_MODULE_PATH'] = str(root / 'modules')
    result, _ = case.report(graph, 'valid', 0, '--target', str(opposite_file))
    shared = next(m for m in result['modules'] if m['metadata']['name'] == 'diamond-leaf')
    assert Path(shared['archive']) == leaf.resolve(), shared
    assert shared['native'][0]['compatibility']['state'] == 'compatible', shared
    assert shared['native'][0]['loading']['state'] == 'not_checked' and not case.events()
    leaf.write_bytes(leaf_bytes)
    checked('target never replaces first native-incompatible archive with a later compatible copy')

    # The same real native archives establish the boundary between graph and local
    # validation. Merge must not consult even an explicitly discoverable graph.
    leaf.unlink()
    case = Case(root, 'missing-transitive', worker)
    case.env['GOAT_MODULE_PATH'] = str(root / 'modules')
    failed, _ = case.report(graph, 'valid', 1)
    assert not case.events(), 'Missing transitive dependency must precede every probe'
    assert all(n['loading']['state'] == 'not_checked' for m in failed['modules']
               for n in (m['native'] or [])), failed
    merged = root / 'repacked diamond root.gbin'
    result = run([MERGE, str(merged), str(graph), str(graph)], env=case.env, input=b'')
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert not result.stderr and not case.events(), (result.stderr, case.events())
    assert len(result.stdout.splitlines()) == 1 and b'49' not in result.stdout.splitlines(), result.stdout
    before_doc, before_files = read_archive(graph)
    merged_doc, merged_files = read_archive(merged)
    assert merged_doc == ref.normalize(before_doc) and merged_files == before_files
    assert merged_doc['mainHash'] == before_doc['mainHash']
    assert not list(case.root.glob('goat-run-*')), list(case.root.iterdir())
    checked('missing transitive dependency rejects inspect but not nonexecuting native merge')

    case = Case(root, 'repacked-still-missing', worker)
    case.env['GOAT_MODULE_PATH'] = str(root / 'modules')
    case.report(merged, 'valid', 1)
    assert not case.events(), 'Repacking must not erase unresolved graph requirements'
    leaf.write_bytes(leaf_bytes)
    case = Case(root, 'repacked-complete', worker)
    repacked, _ = case.report(merged, 'valid', 0, '--module-dir', str(root / 'modules'))
    assert len(repacked['modules']) == 4 and case.events() == 'loaded\nunloaded\n' * 4
    assert {m['metadata']['mainHash'] for m in repacked['modules']} == {
        m['metadata']['mainHash'] for m in report['modules']}
    assert all(n['loading']['probe']['state'] == 'passed'
               for m in repacked['modules'] for n in m['native'])
    checked('restored dependencies make merged archive inspectable with one probe per unique node')

    leaf.write_bytes(b'corrupted reachable archive')
    case = Case(root, 'invalid-graph', worker)
    report, _ = case.report(graph, 'valid', 1, '--module-dir', str(root / 'modules'))
    assert not case.events(), 'Graph errors must prevent all native constructors'
    assert all(n['loading']['state'] == 'not_checked' for m in report['modules']
               for n in (m['native'] or [])), report
    checked('whole graph validation precedes every OS native load')

    case = Case(root, 'text-output', worker)
    case.env.update(GOAT_PROBE_TEST_MODE='output', GOAT_PROBE_TEST_BYTES='257')
    text = run([EXE, str(valid)], env=case.env)
    assert text.returncode == 0, text
    combined = text.stdout + text.stderr
    assert b'\x1b' not in combined and b'\x00' not in combined, text
    combined.decode('utf-8')
    checked('text capture safely escapes arbitrary binary/terminal bytes')

    for fault, expected, state, stage in [
            ('start', 1, 'failed', 'start'), ('capture', 1, 'failed', 'capture'),
            ('unload', 0, 'failed', 'unload'), ('diagnostic', 0, 'failed', 'unload'), ('exit-nonzero', 0, 'crashed', 'exit'),
            ('exit-hang', 0, 'timeout', 'exit')]:
        case = Case(root, 'fault-' + fault, worker)
        case.exe = HOST
        case.env['GOAT_INSPECT_PROBE_FAULT'] = fault
        probe, _ = case.loading(valid, 'valid', expected, '--probe-timeout', '1')
        assert probe['state'] == state and probe['stage'] == stage, probe
        if fault == 'diagnostic':
            assert probe['diagnostics'][0]['message'].startswith('\\xe2\\xff'), probe
        if fault.startswith('exit-'):
            assert probe['completedStages'] == ['load', 'descriptor', 'symbols', 'unload'], probe
        checked('injected ' + fault + ': service progress alone never certifies child success')

    for fault in ('exit', 'silent'):
        case = Case(root, 'cleaner-start-' + fault, worker)
        case.exe = HOST
        case.env['GOAT_INSPECT_CLEANER_FAULT'] = fault
        report, elapsed = case.report(valid, 'valid', 1)
        assert not case.events() and elapsed < 10, (report, elapsed)
        assert not list(case.root.glob('goat-run-*'))
        checked('janitor startup ' + fault + ': no extracted files or native execution')

    # Keep actual removal unavailable until after the public five-second wait.
    # The inspector reports the outcome separately and does not kill the janitor.
    for mode, state in [('valid', 'passed'), ('load-crash', 'crashed')]:
        case = Case(root, 'cleanup-timeout-' + mode, worker)
        case.exe = HOST
        gate = case.marker / 'allow-cleanup'
        marker = case.marker / 'cleanup-started'
        cleaner_pid = case.marker / 'cleaner.pid'
        case.env.update(GOAT_INSPECT_PROBE_CLEANUP_GATE=str(gate),
                        GOAT_INSPECT_PROBE_CLEANUP_MARKER=str(marker),
                        GOAT_INSPECT_CLEANER_PID=str(cleaner_pid))
        cleaner = None
        try:
            report, elapsed = case.report(valid, mode, 1)
            assert 4 <= elapsed < 15, elapsed
            cleaner = ProcessWatch(int(cleaner_pid.read_text()))
            assert cleaner.alive(), 'Timed-out janitor was terminated by caller'
            loading = report['modules'][0]['native'][0]['loading']
            assert loading['probe']['state'] == state, report
            cleanup = loading['cleanup']
            assert cleanup['state'] == 'failed' and cleanup['diagnostics'], cleanup
            temporary_directory = Path(cleanup['temporaryDirectory'])
            assert temporary_directory.is_absolute() and temporary_directory.exists(), cleanup
            assert temporary_directory == Path(marker.read_text().strip()), cleanup
            assert cleanup['remainingPaths'] == [], cleanup
            gate.touch()
            until(lambda: not temporary_directory.exists(), 15)
            cleaner.wait(15)
        finally:
            gate.touch(exist_ok=True)
            if cleaner:
                cleaner.wait(20)
                cleaner.close()
        checked('cleanup timeout preserves ' + state + ' probe, janitor continues and eventually removes files')

    case = Case(root, 'owner-killed-before-extraction', worker)
    case.exe = HOST
    marker = case.marker / 'ready'
    gate = case.marker / 'admit-extraction'
    case.env.update(GOAT_INSPECT_PROBE_READY_MARKER=str(marker),
                    GOAT_INSPECT_PROBE_READY_GATE=str(gate))
    owner = subprocess.Popen(case.command(valid), env=case.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        until(lambda: ready_file(marker), 10)
        directory = Path(marker.read_text().strip())
        assert directory.exists() and not case.events()
        assert not list(directory.iterdir()), 'Extraction occurred before READY admission barrier'
        owner.kill()
        owner.communicate(timeout=15)
        until(lambda: not directory.exists(), 15)
    finally:
        if owner.poll() is None:
            owner.kill()
            owner.communicate(timeout=10)
    checked('owner termination after READY but before extraction is cleaned by independent janitor')

    independent_markers = root / 'independent'
    independent_markers.mkdir()
    independent = subprocess.Popen([str(worker)], env=dict(os.environ,
        GOAT_PROBE_TEST_MARKERS=str(independent_markers)), stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        until(lambda: ready_file(independent_markers / 'descendant'), 10)
        for mode, state in [('tree', 'passed'), ('tree-crash', 'crashed'), ('tree-hang', 'timeout')]:
            case = Case(root, mode, worker)
            probe, _ = case.loading(valid, mode, 0, '--probe-timeout', '1')
            assert probe['state'] == state, probe
            case.descendants()
            checked(mode + ': inherited output pipes do not keep children alive or delay cleanup')

            assert independent.poll() is None, 'Probe cleanup killed an unrelated process'
    finally:
        if independent.poll() is None:
            independent.kill()
        independent.wait(timeout=10)

    # A killed owner cannot run its own finally blocks; its independent janitor must act.
    case = Case(root, 'owner-killed', worker)
    case.env['GOAT_PROBE_TEST_MODE'] = 'tree-hang'
    owner = subprocess.Popen(case.command(valid, '--probe-timeout', '60'), env=case.env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    descendant = None
    try:
        until(lambda: ready_file(case.marker / 'descendant'), 10)
        descendant = ProcessWatch(int((case.marker / 'descendant').read_text()))
        (case.marker / 'descendant-pinned').touch()
        directories = list(case.root.glob('goat-run-*'))
        assert directories, 'No temporary work directory while child is running'
        owner.kill()
        owner.communicate(timeout=15)
        descendant.wait(15)
        until(lambda: all(not path.exists() for path in directories), 15)
    finally:
        if owner.poll() is None:
            owner.kill()
            owner.communicate(timeout=10)
        if descendant:
            if descendant.alive():
                descendant.kill()
                descendant.wait(10)
            descendant.close()
    checked('external inspect termination: independent janitor owns live probe tree and temporary directory')

print(f'{COUNT} isolated native inspection checks passed.')
