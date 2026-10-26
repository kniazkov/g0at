#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Real module snapshots, lazy native loading, failure isolation and cleaner lifecycle."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import zipfile

from process_test_support import ProcessWatch, until

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

RUNNER = str(Path(sys.argv[1]).resolve())
BUILDER = str(Path(sys.argv[2]).resolve())
CC = shutil.which(sys.argv[3]) or sys.argv[3]
COUNT = 0


def checked(name):
    global COUNT
    COUNT += 1
    print('[ ok ]', name, flush=True)


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
        used = {'bytecode.bin'} | {v['library'] for v in doc.get('native', [])}
        for name in sorted(used):
            archive.writestr(name, files[name])
    return path


def build(root, name, source, mode='required', dependencies=()):
    directory = root / name
    directory.mkdir()
    for dep in dependencies:
        doc, _ = read_archive(dep)
        dest = directory / 'modules' / doc['name'] / (doc['version'] + '.gbin')
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(dep, dest)
    (directory / 'main.goat').write_text(source, encoding='utf-8')
    descriptor = directory / 'module.gmod'
    descriptor.write_text(json.dumps(dict(formatVersion=1, name=name, version='1', main='main.goat',
                                         native=dict(mode=mode))), encoding='utf-8')
    archive = directory / 'module.gbin'
    run = subprocess.run([BUILDER, 'native-build', str(descriptor), str(archive),
                          'descriptor', 'descriptor', CC], capture_output=True, timeout=120)
    assert run.returncode == 0, (name, run.returncode, run.stdout, run.stderr)
    assert not run.stdout
    return archive


def custom(root, name, original, kind='valid', edit=None):
    doc, files = read_archive(original)
    if edit:
        edit(doc, files)
    variant = doc['native'][0]
    description = dict(formatVersion=1, goatAbiVersion=1, module=doc['name'], version=doc['version'],
                       mainHash=doc['mainHash'], target=variant['target'],
                       specializations=variant['specializations'])
    if kind == 'descriptor-mismatch':
        description['mainHash'] = '0' * 64
    encoded = ref.canonical(description)
    blob = b'GOATND01' + struct.pack('<Q', len(encoded)) + encoded + b'\0'
    if kind == 'descriptor-overflow':
        blob = b'GOATND01' + b'\xff' * 8 + b'\0'
    if kind == 'descriptor-short':
        blob = b'X'
    parts = ['#include "goat/module_abi.h"', '#include <stdio.h>', '#include <stdlib.h>',
             'static void mark(char c) { const char *p=getenv("GOAT_LOADING_NATIVE_MARKER");'
             ' if(p){FILE *f=fopen(p,"ab");if(f){fputc(c,f);fclose(f);}}}',
             '__attribute__((constructor)) static void loaded(void) { mark(\'L\'); }']
    if kind != 'descriptor-missing':
        qualifier = '' if kind == 'descriptor-writable' else 'const '
        parts.append('GOAT_MODULE_EXPORT ' + qualifier + 'unsigned char goat_module_descriptor_v1[] = {'
                     + ','.join(map(str, blob)) + '};')
    for index, spec in enumerate(variant['specializations']):
        if kind == 'symbol-missing' and index == len(variant['specializations']) - 1:
            continue
        symbol = spec['symbol']
        parts.append('GOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL ' + symbol +
                     '(uint32_t abi,const goat_module_context_v1*c,uint32_t n,'
                     'const goat_module_value_v1*a,goat_module_value_v1*r){'
                     '(void)abi;(void)c;(void)n;mark(\'C\');')
        if kind == 'bad-status':
            parts.append('return GOAT_MODULE_RESOURCE_LIMIT;}')
        elif kind == 'bad-result':
            parts.append('r->tag=GOAT_MODULE_STRING;r->payload=1;return 0;}')
        else:
            parts.append('r->tag=GOAT_MODULE_INTEGER;r->reserved=0;'
                         'r->payload=a[0].payload*a[0].payload;return 0;}')
    path = root / (name + ('.dll' if os.name == 'nt' else '.so'))
    source = root / (name + '.c')
    source.write_text('\n'.join(parts), encoding='utf-8')
    command = [CC, '-std=c11', '-shared', '-O0', '-I', str(ROOT / 'include'), str(source), '-o', str(path)]
    if os.name != 'nt':
        command.insert(1, '-fPIC')
    run = subprocess.run(command, capture_output=True, timeout=60)
    assert run.returncode == 0, (command, run.stdout, run.stderr)
    files[variant['library']] = path.read_bytes()
    return save_archive(root / (name + '.gbin'), doc, files)


class Owner:
    def __init__(self, root, name, archive, mode='auto', fault='run', **env):
        self.directory = root / ('owner-' + name)
        self.directory.mkdir()
        self.marker = self.directory / 'native.calls'
        self.env = dict(os.environ, TMPDIR=str(self.directory), TMP=str(self.directory),
                        TEMP=str(self.directory), GOAT_LOADING_MARKERS=str(self.directory),
                        GOAT_LOADING_NATIVE_MARKER=str(self.marker), **env)
        self.process = subprocess.Popen([RUNNER, str(archive), mode, fault], env=self.env,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.output = queue.Queue()
        self.lines = []
        self.watches = []
        def read():
            for line in self.process.stdout:
                decoded = line.decode().rstrip('\r\n')
                self.lines.append(decoded)
                self.output.put(decoded)
            self.output.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def line(self):
        value = self.output.get(timeout=40)
        assert value is not None, (self.process.poll(), self.process.stderr.read())
        return value

    def resume(self):
        self.process.stdin.write(b'G\n')
        self.process.stdin.flush()

    def mark(self, name):
        path = self.directory / name
        return until(lambda: path.read_text(encoding='utf-8').strip() if path.exists() else None,
                     timeout=40)

    def watch(self):
        watch = ProcessWatch(int(self.mark('cleaner.pid')))
        self.watches.append(watch)
        return watch

    def end(self, code=0, cleanup=True):
        self.process.stdin.close()
        actual = self.process.wait(timeout=120)
        stderr = self.process.stderr.read()
        self.reader.join(timeout=5)
        assert actual == code, (actual, code, self.lines, stderr)
        records = [line[7:] for line in stderr.splitlines() if line.startswith(b'RESULT ')]
        assert len(records) == 1, stderr
        result = json.loads(records[0])
        result['stderr'] = stderr
        if cleanup and result.get('directory'):
            until(lambda: not Path(result['directory']).exists(), timeout=40)
            self.mark('cleaner.done')
        return result

    def calls(self):
        return self.marker.read_bytes() if self.marker.exists() else b''

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=30)
        for watch in self.watches:
            watch.wait(timeout=40)
            watch.close()
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()


with tempfile.TemporaryDirectory(prefix='goat-loading-') as temporary:
    root = Path(temporary).resolve()
    owners = []
    def start(name, archive, mode='auto', fault='run', **env):
        owner = Owner(root, name, archive, mode, fault, **env)
        owners.append(owner)
        return owner
    def run(name, archive, mode='auto', fault='run', code=0, output=('49', '64'), **env):
        owner = start(name, archive, mode, fault, **env)
        result = owner.end(code)
        assert tuple(owner.lines) == output, (name, owner.lines, result)
        return owner, result

    try:
        base = build(root, 'base', 'external f=func(x){return x*x;}; println(f(7)); println(f(8));')
        valid = custom(root, 'valid', base)
        owner, result = run('off', valid, 'off')
        assert not owner.calls() and not result['directory'] and not result['writes']
        assert not (owner.directory / 'cleaner.pid').exists()
        checked('off executes VM without cleaner, extraction or OS loader')

        for mode in ('auto', 'required'):
            owner, result = run('valid-' + mode, valid, mode)
            assert owner.calls() == b'LCC', (owner.calls(), result)
            assert result['writes'] == 1 and result['reports'] == 0
            assert result['nodes'][0]['loads'] == result['nodes'][0]['extractions'] == 1
            assert result['nodes'][0]['loaded']
            checked(mode + ': first applicable call loads once and reuses library; cleaner removes files')

        def nonmatching(doc, files):
            for spec in doc['native'][0]['specializations']:
                spec['signature']['arguments'] = ['real']
                spec['when'] = dict(argumentTypes=['real'])
        no_match = custom(root, 'no-match', base, edit=nonmatching)
        for mode in ('auto', 'required'):
            owner, result = run('no-match-' + mode, no_match, mode)
            assert not owner.calls() and not result['directory'] and not result['writes']
            checked(mode + ': inapplicable own specialization never loads and still permits VM')

        doc, files = read_archive(base)
        doc['native'] = []
        plain = save_archive(root / 'plain.gbin', doc, files)
        owner, result = run('plain-auto', plain)
        assert not result['writes'] and not result['directory']
        owner, result = run('plain-required', plain, 'required', code=3, output=())
        assert result['prepare'] and not (owner.directory / 'cleaner.pid').exists()
        checked('required own inventory preflight rejects before initializer side effects')

        doc, files = read_archive(base)
        doc['native'][0]['target']['minimumOs'] = [999, 0]
        foreign = save_archive(root / 'foreign.gbin', doc, files)
        owner, result = run('foreign-auto', foreign)
        assert not result['writes']
        run('foreign-required', foreign, 'required', code=3, output=())
        checked('incompatible variant: auto VM, required rejects without loading')

        doc, files = read_archive(base)
        alternate = copy.deepcopy(doc['native'][0])
        alternate['library'] = 'native/alternate' + Path(alternate['library']).suffix
        files[alternate['library']] = files[doc['native'][0]['library']]
        doc['native'].append(alternate)
        ambiguous = save_archive(root / 'ambiguous.gbin', doc, files)
        for mode in ('auto', 'required'):
            run('ambiguous-' + mode, ambiguous, mode, code=3, output=())
        run('ambiguous-off', ambiguous, 'off')
        checked('platform ambiguity is fatal rather than first-match; off never selects')

        doc, files = read_archive(base)
        name = doc['native'][0]['library']
        files[name] = b'corrupted' + files[name][9:]
        damaged = root / 'damaged.gbin'
        with zipfile.ZipFile(damaged, 'w', compression=zipfile.ZIP_STORED) as archive:
            archive.writestr('module.json', ref.canonical(doc))
            for name, value in files.items():
                archive.writestr(name, value)
        for mode in ('off', 'auto', 'required'):
            owner, result = run('damaged-' + mode, damaged, mode, code=2, output=())
            assert result['graph'] and not (owner.directory / 'cleaner.pid').exists()
        checked('native hash damage is fatal in all modes before any execution')

        replacement = root / 'replace.gbin'
        shutil.copyfile(valid, replacement)
        owner = start('frozen', replacement, 'required', 'frozen')
        assert owner.line() == 'READY'
        replacement.write_bytes(b'changed after snapshot')
        owner.resume()
        result = owner.end()
        assert owner.lines == ['READY', '49', '64'] and owner.calls() == b'LCC'
        checked('lazy extraction uses retained verified bytes after archive replacement')

        doc, files = read_archive(base)
        files[doc['native'][0]['library']] = b'not an OS library'
        invalid_os = save_archive(root / 'invalid-os.gbin', doc, files)
        for kind, archive in [('os', invalid_os)] + [(kind, custom(root, kind, base, kind)) for kind in
                ('descriptor-missing', 'descriptor-mismatch', 'descriptor-overflow',
                 'descriptor-short', 'descriptor-writable', 'symbol-missing')]:
            owner, result = run(kind + '-auto', archive)
            assert result['reports'] == 1 and result['writes'] == 1, (kind, result)
            assert result['nodes'][0]['unavailable'] and result['nodes'][0]['loads'] == 1
            assert b'C' not in owner.calls(), (kind, owner.calls())
            owner, result = run(kind + '-required', archive, 'required', code=1, output=())
            assert b'C' not in owner.calls()
            checked(kind + ': one failed preparation, auto VM once/report once, required terminal')

        for kind in ('bad-status', 'bad-result'):
            archive = custom(root, kind, base, kind)
            for mode in ('auto', 'required'):
                owner, result = run(kind + '-' + mode, archive, mode, code=1, output=())
                assert owner.calls() == b'LC' and result['terminal'], (owner.calls(), result)
            checked(kind + ': entered native call never replays VM after terminal ABI failure')

        for fault in ('write-before', 'write-partial'):
            for mode in ('auto', 'required'):
                owner, result = run(fault + '-' + mode, valid, mode, fault,
                                    code=0 if mode == 'auto' else 1,
                                    output=('49', '64') if mode == 'auto' else ())
                assert result['writes'] == 1 and not owner.calls()
                assert result['nodes'][0]['loads'] == 0
                if mode == 'auto':
                    assert result['reports'] == 1
            checked(fault + ': no partial OS loading, no retry, and cleaner removes remnants')

        for fault in ('exit', 'silent'):
            for mode in ('auto', 'required'):
                owner, result = run('cleaner-' + fault + '-' + mode, valid, mode,
                                    code=0 if mode == 'auto' else 1,
                                    output=('49', '64') if mode == 'auto' else (),
                                    GOAT_LOADING_CLEANER_FAULT=fault)
                assert not result['writes'] and not owner.calls()
                assert result['reports'] == (1 if mode == 'auto' else 0), result
            checked('cleaner ' + fault + ': sticky pre-READY failure, no extraction or loader')

        for mode in ('auto', 'required'):
            owner, result = run('real-producer-' + mode, base, mode)
            assert result['nodes'][0]['loaded'] and result['nodes'][0]['loads'] == 1
        checked('real compiler-produced module adapters execute through production loader')
        unicode_archive = root / 'модуль-😀.gbin'
        shutil.copyfile(base, unicode_archive)
        owner, result = run('unicode-данные-😀', unicode_archive, 'required')
        assert result['nodes'][0]['loaded'] and result['writes'] == 1
        checked('Unicode archive and temporary directory paths work through the actual OS loader')
        for kind, source, expected in (
                ('allocated-string', 'external text=func(x){return x+"😀";}; println(text("abc"));', 'abc😀'),
                ('static-string', 'external text=func(x){return "literal 😀";}; println(text("abc"));', 'literal 😀')):
            archive = build(root, kind, source)
            for mode in ('auto', 'required'):
                owner, result = run(kind + '-' + mode, archive, mode, output=(expected,))
                assert result['nodes'][0]['loaded'] and result['nodes'][0]['loads'] == 1
                assert not result['reports'] and not result['terminal']
            checked(kind + ': actual generated string adapter validates storage and Unicode result')

        template, template_files = read_archive(base)
        def graft(name, source):
            archive = build(root, name, source, 'off')
            doc, files = read_archive(archive)
            doc['native'] = copy.deepcopy(template['native'])
            files[doc['native'][0]['library']] = template_files[template['native'][0]['library']]
            save_archive(archive, doc, files)
            generated = custom(root, name + '-library', archive)
            shutil.copyfile(generated, archive)
            return archive

        dormant = graft('dormant', 'external f=func(x){return x*x;}; println("DONE");')
        owner, result = run('dormant', dormant, 'required', output=('DONE',))
        assert not result['writes'] and not result['directory'] and not owner.calls()
        checked('required unused own specialization passes metadata preflight without loading')

        concurrent = graft('concurrent', 'external f=func(x){return x*x;}; '
                            'const a=spawn(f,7); const b=spawn(f,8); println(join(a)); println(join(b));')
        for mode in ('auto', 'required'):
            owner, result = run('concurrent-' + mode, concurrent, mode)
            assert owner.calls() == b'LCC' and result['writes'] == 1
            assert result['nodes'][0]['loads'] == 1
        checked('concurrent first calls share one extraction, OS load and cleaner')

        dependency = graft('dependency', 'external f=func(x){return x*x;};')
        app = build(root, 'consumer', 'external own=func(x){return x*x;}; println(own(5)); '
                    'const dep=import "dependency@1"; println(dep.f(7)); println(dep.f(8));',
                    dependencies=(dependency,))
        dep_path = app.parent / 'modules' / 'dependency' / '1.gbin'
        for mode in ('auto', 'required'):
            owner, result = run('dependency-valid-' + mode, app, mode, output=('25','49','64'))
            assert len(result['nodes']) == 2 and all(n['loaded'] for n in result['nodes'])
            assert result['writes'] == 2 and owner.calls() == b'LCC'
        checked('own and dependency libraries load independently and share run directory')

        imported = build(root, 'import-only', 'external own=func(x){return x*x;}; println(own(5)); '
                         'const dep=import "dependency@1"; println("IMPORTED");', dependencies=(dependency,))
        owner, result = run('import-only', imported, 'required', output=('25','IMPORTED'))
        assert result['writes'] == 1 and not owner.calls() and not result['nodes'][1]['loaded']
        checked('importing a compatible native dependency does not extract or OS-load it')

        doc, files = read_archive(app)
        doc['native'] = []
        dep_only = save_archive(app.parent / 'dependency-only.gbin', doc, files)
        owner, result = run('dependency-only', dep_only, 'required', code=3, output=())
        assert not owner.calls() and not (owner.directory / 'cleaner.pid').exists()
        checked('dependency native inventory never satisfies required root preflight')

        original_dep = dependency.read_bytes()
        doc, files = read_archive(dependency)
        for kind in ('bytecode', 'foreign'):
            modified = copy.deepcopy(doc)
            if kind == 'bytecode':
                modified['native'] = []
            else:
                modified['native'][0]['target']['minimumOs'] = [999, 0]
            save_archive(dep_path, modified, files)
            owner, result = run('dependency-' + kind, app, 'required', output=('25','49','64'))
            assert result['writes'] == 1 and not owner.calls()
        dep_path.write_bytes(original_dep)
        checked('required accepts bytecode-only and foreign-platform dependencies')

        for kind in ('os', 'symbol-missing', 'descriptor-mismatch'):
            if kind == 'os':
                doc, files = read_archive(dependency)
                files[doc['native'][0]['library']] = b'not a shared library'
                save_archive(dep_path, doc, files)
            else:
                damaged_dep = custom(root, 'dependency-' + kind, dependency, kind)
                shutil.copyfile(damaged_dep, dep_path)
            for mode in ('auto', 'required'):
                owner, result = run('dep-failure-' + kind + '-' + mode, app, mode,
                                    code=0 if mode == 'auto' else 1,
                                    output=('25','49','64') if mode == 'auto' else ('25',))
                assert result['nodes'][0]['loaded'] and result['nodes'][1]['unavailable']
                assert result['nodes'][1]['loads'] == 1 and b'C' not in owner.calls()
            checked('dependency ' + kind + ': isolated auto fallback and required terminal error')
        dep_path.write_bytes(original_dep)

        doc, files = read_archive(dependency)
        doc['native'][0]['target']['minimumOs'] = [999, 0]
        library_name = doc['native'][0]['library']
        files[library_name] = b'changed without updating hash'
        with zipfile.ZipFile(dep_path, 'w', compression=zipfile.ZIP_STORED) as archive:
            archive.writestr('module.json', ref.canonical(doc))
            for name, value in files.items():
                archive.writestr(name, value)
        for mode in ('off', 'auto', 'required'):
            owner, result = run('foreign-corrupt-dependency-' + mode, app, mode, code=2, output=())
            assert result['graph'] and not owner.calls()
            assert not (owner.directory / 'cleaner.pid').exists()
        dep_path.write_bytes(original_dep)
        checked('whole-graph preflight rejects corrupted foreign dependency before root effects')

        for fault in ('write-before', 'write-partial'):
            owner, result = run('isolated-' + fault, app, fault=fault, output=('25','49','64'))
            assert result['reports'] == 1 and result['writes'] == 2
            assert result['nodes'][0]['unavailable'] and result['nodes'][1]['loaded']
            assert owner.calls() == b'LCC'
        checked('failed extraction disables only its library; another module still uses native')

        for fault in ('exit', 'silent'):
            owner, result = run('global-' + fault, app, output=('25','49','64'),
                                GOAT_LOADING_CLEANER_FAULT=fault)
            assert result['reports'] == 1 and not result['writes'] and not owner.calls()
        checked('cleaner readiness failure disables all modules once without repeated warnings')

        for mode in ('auto', 'required'):
            owner, result = run('integrity-' + mode, valid, mode, 'write-corrupt', code=1, output=())
            assert result['terminal'] and not owner.calls() and result['nodes'][0]['loads'] == 0
        checked('corrupt successful extraction is terminal even in auto, never VM fallback')

        two = build(root, 'two-symbols', 'external f=func(x){return x*x;}; external g=func(x){return x+1;}; '
                    'println(f(7)); println(g(8));')
        broken = custom(root, 'missing-last', two, 'symbol-missing')
        owner, result = run('all-symbols', broken, output=('49','9'))
        assert b'C' not in owner.calls() and result['reports'] == 1
        checked('all declared symbols are verified before any earlier valid adapter executes')

        for fault in ('exit-write', 'exit-write-fail'):
            for mode in ('auto', 'required'):
                owner, result = run('pending-' + fault + '-' + mode, valid, mode, fault,
                                    code=23, output=())
                assert b'C' not in owner.calls() and not result['terminal']
            checked(fault + ': termination during extraction prevents native entry and preserves code')

        owner, result = run('concurrent-write-fail', concurrent, fault='write-partial')
        assert result['writes'] == result['reports'] == 1 and not owner.calls()
        owner, result = run('concurrent-cleaner-fail', concurrent,
                            GOAT_LOADING_CLEANER_FAULT='silent')
        assert result['reports'] == 1 and not result['writes'] and not owner.calls()
        checked('concurrent failures publish one shared unavailable result without retry')

        caught = graft('caught', 'external f=func(x){return x*x;}; '
                       'try { println(f(7)); } catch(e) { println("CAUGHT"); } println("AFTER");')
        bad_caught = custom(root, 'caught-status', caught, 'bad-status')
        for mode in ('auto', 'required'):
            owner, result = run('caught-' + mode, bad_caught, mode, code=1, output=())
            assert result['terminal'] and owner.calls() == b'LC'
        checked('real adapter ABI failure cannot be hidden by initializer catch')

        for mode in ('auto', 'required'):
            for explicit_exit in (False, True):
                source = ('external f=func(x){return x*x;}; println(f(7)); println("READY"); '
                          'input(); println(f(8));' + ('exit(23);' if explicit_exit else ''))
                archive = build(root, 'continued-' + mode + str(int(explicit_exit)), source)
                owner = start('continued-' + mode + str(int(explicit_exit)), archive, mode)
                assert owner.line() == '49' and owner.line() == 'READY'
                cleaner = owner.watch()
                directory = until(lambda: next(owner.directory.glob('goat-run-*'), None))
                assert directory.is_dir() and list(directory.iterdir())
                cleaner.kill()
                cleaner.wait()
                owner.resume()
                result = owner.end(23 if explicit_exit else 0, cleanup=False)
                assert owner.lines == ['49','READY','64'] and result['nodes'][0]['loaded']
                assert directory.is_dir()
                shutil.rmtree(directory)
        checked('after cleaner death user code and native calls continue; normal/exit codes preserved')

        late = graft('late-worker', 'external f=func(x){return x*x;}; '
                     'const t=spawn(func(){input();println(f(7));}); println("ENTRY-END");')
        owner = start('late-worker', late, 'required')
        assert owner.line() == 'ENTRY-END'
        assert not (owner.directory / 'cleaner.pid').exists() and not owner.calls()
        owner.resume()
        result = owner.end()
        assert owner.lines == ['ENTRY-END','49'] and owner.calls() == b'LC'
        assert result['nodes'][0]['loaded']
        checked('first lazy native call after entry thread ends retains snapshot until final worker')

        owner = start('killed-owner', valid, 'required', 'hold')
        assert owner.line() == '49' and owner.line() == '64' and owner.line() == 'HOLD'
        directory = Path(owner.mark('directory'))
        cleaner = owner.watch()
        assert directory.is_dir()
        owner.process.kill()
        owner.process.wait(timeout=30)
        cleaner.wait(timeout=40)
        until(lambda: not directory.exists(), timeout=40)
        checked('forced owner termination leaves cleanup to its prestarted independent cleaner')

        for mode in ('auto', 'required'):
            owner = start('held-' + mode, valid, mode, 'hold')
            assert owner.line() == '49' and owner.line() == '64' and owner.line() == 'HOLD'
            directory = Path(owner.mark('directory'))
            assert directory.is_dir() and list(directory.iterdir())
            cleaner = owner.watch()
            assert cleaner.alive()
            cleaner.kill()
            cleaner.wait()
            assert directory.is_dir()
            owner.resume()
            result = owner.end(cleanup=False)
            assert result['status'] == 0 and owner.calls() == b'LCC'
            shutil.rmtree(directory)
            checked(mode + ': cleaner death after READY does not change owner result or unload resources')
    finally:
        for owner in owners:
            owner.close()

print(f'{COUNT} lazy module loading scenario groups passed')
