#!/usr/bin/env python3
"""#211 native consumers: synchronized cleaner/compiler lifetime, failure policy and artifacts."""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading

from process_test_support import ProcessWatch, until

fixture = str(Path(sys.argv[1]).resolve())
compiler = str(Path(shutil.which(sys.argv[2] if len(sys.argv) > 2 else os.environ.get('CC', 'cc'))).resolve())
passed = 0


def ok(text):
    global passed
    passed += 1
    print('[ ok ]', text, flush=True)


class Owner:
    def __init__(self, root, name, arguments, **environment):
        self.root = root / name
        self.root.mkdir()
        self.watches = []
        self.env = dict(os.environ, TMPDIR=str(self.root), TMP=str(self.root), TEMP=str(self.root),
                        GOAT_NATIVE_TEST_MARKERS=str(self.root), **environment)
        self.process = subprocess.Popen([fixture, *map(str, arguments)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=self.env)
        self.lines = queue.Queue()
        def reader():
            for line in self.process.stdout:
                self.lines.put(line.decode().rstrip('\r\n'))
            self.lines.put(None)
        threading.Thread(target=reader, daemon=True).start()

    def line(self):
        value = self.lines.get(timeout=30)
        assert value is not None, (self.process.poll(), self.process.stderr.read())
        return value

    def marker(self, name):
        def read():
            try:
                value = (self.root / name).read_text(encoding='utf-8').strip()
                return value or None
            except FileNotFoundError:
                return None
        return until(read, timeout=30)

    def watch(self, name):
        watch = ProcessWatch(int(self.marker(name)))
        self.watches.append(watch)
        return watch

    def end(self, code=0):
        self.process.stdin.close()
        assert self.process.wait(timeout=30) == code, self.process.stderr.read()
        error = self.process.stderr.read()
        assert b'leak' not in error.lower(), error
        return error

    def continue_run(self):
        self.process.stdin.write(b'go\n')
        self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=30)
        for watch in self.watches:
            # The cleaner may intentionally be killed; otherwise owner death must stop the tree.
            watch.wait(timeout=40)
            watch.close()
        if not self.process.stdin.closed:
            self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()


with tempfile.TemporaryDirectory(prefix='goat-native-cleanup-данные-') as directory:
    root = Path(directory).resolve()
    owners = []
    def start(name, args, **env):
        owner = Owner(root, name, args, **env)
        owners.append(owner)
        return owner
    program = root / 'program.goat'
    program.write_text('const f=func(n){return n+1;}; println("READY"); input(); println(f(8));', encoding='utf-8')
    empty = root / 'empty.goat'
    empty.write_text('println("SIDE");', encoding='utf-8')
    try:
        no_native = start('empty', ['--native', 'required', empty])
        assert no_native.end(1)
        assert no_native.lines.get(timeout=2) is None
        assert not (no_native.root / 'cleaner.pid').exists()
        assert not list(no_native.root.glob('goat-run-*'))
        ok('required rejects an empty own inventory before side effects and before cleaner startup')

        for fault in ('exit', 'silent'):
            for mode in ('auto', 'required'):
                owner = start(f'failure-{fault}-{mode}', ['--native', mode, '--cc', fixture, program],
                              GOAT_NATIVE_TEST_CLEANER_FAULT=fault)
                if mode == 'auto':
                    assert owner.line() == 'READY'
                    owner.continue_run()
                    assert owner.line() == '9'
                error = owner.end(0 if mode == 'auto' else 1)
                assert error.count(b'Native preparation failed (cleanup-error)') == 1, error
                assert not (owner.root / 'compiler.query').exists()
                assert not list(owner.root.glob('goat-run-*'))
        ok('pre-READY exit/timeout: one auto warning and VM, required error, no compiler or build files')

        for mode in ('auto', 'required'):
            for explicit_exit in (False, True):
                source = root / f'code-{mode}-{explicit_exit}.goat'
                source.write_text(program.read_text() + ('exit(23);' if explicit_exit else ''), encoding='utf-8')
                report = root / f'report-{mode}-{explicit_exit}.txt'
                owner = start(f'death-{mode}-{explicit_exit}', ['--native', mode, '--cc', compiler,
                              '--save-native', report, source])
                assert owner.line() == 'READY'
                cleaner = owner.watch('cleaner.pid')
                artifacts = list(owner.root.glob('goat-run-*/native*/*'))
                if not artifacts:
                    # READY is user output, not proof that auto selected native code.
                    # Finish the blocked program to expose a useful preparation diagnostic.
                    owner.continue_run()
                    error = owner.end(23 if explicit_exit else 0)
                    raise AssertionError(('native workspace missing', str(owner.root), error,
                                          report.read_text() if report.exists() else 'no report'))
                cleaner.kill()
                cleaner.wait()
                owner.continue_run()
                assert owner.line() == '9'
                error = owner.end(23 if explicit_exit else 0)
                assert not error, error
                if explicit_exit:
                    # Exit never returns through host-side report/destructor code.
                    assert not report.exists(), report
                else:
                    fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
                    assert int(fields['succeeded']) > 0, fields
        ok('cleaner death after native READY: auto/required keep real native execution and normal/exit status')

        for explicit_exit in (False, True):
            source = root / f'normal-{explicit_exit}.goat'
            source.write_text(program.read_text() + ('exit(23);' if explicit_exit else ''), encoding='utf-8')
            normal = start(f'normal-{explicit_exit}', ['--native', 'required', '--cc', compiler, source])
            assert normal.line() == 'READY'
            cleaner = normal.watch('cleaner.pid')
            normal.continue_run()
            assert normal.line() == '9'
            error = normal.end(23 if explicit_exit else 0)
            assert not error, error
            cleaner.wait()
            assert not list(normal.root.glob('goat-run-*'))
        ok('ordinary/exit completion: cleaner waits for owner death before deleting intermediates')

        # The public CLI runs module descriptors in memory and archives via the
        # verified snapshot loader. Both share the exact same prestarted cleaner.
        for explicit_exit in (False, True):
            project = root / ('module-exit-' + str(explicit_exit))
            project.mkdir()
            (project / 'main.goat').write_text(
                'external f=func(n){return n+1;}; println(f(8)); '
                'println("READY"); input(); println(f(9)); '
                + ('try { exit(23); } finally { println("UNREACHABLE"); }'
                   if explicit_exit else ''), encoding='utf-8')
            descriptor = project / 'module.gmod'
            descriptor.write_text(json.dumps(dict(formatVersion=1, name='cleanup-module',
                                                   version='1', main='main.goat')), encoding='utf-8')
            producer = start('module-build-' + str(explicit_exit),
                             ['build', descriptor, '--native', 'required', '--cc', compiler])
            assert not producer.end()
            assert producer.marker('cleaner.done') == '0'
            assert not list(producer.root.glob('goat-run-*'))
            archive = project / 'modules/cleanup-module/1.gbin'
            assert archive.is_file()
            for form, path in [('descriptor', descriptor), ('archive', archive)]:
                for mode in ('auto', 'required'):
                    owner = start(f'module-{form}-{mode}-{explicit_exit}',
                                  ['run', path, '--native', mode, '--cc', compiler])
                    assert owner.line() == '9' and owner.line() == 'READY'
                    cleaner = owner.watch('cleaner.pid')
                    directories = list(owner.root.glob('goat-run-*'))
                    assert len(directories) == 1 and cleaner.alive(), directories
                    libraries = [item for item in directories[0].rglob('*')
                                 if item.suffix in ('.dll', '.so')]
                    assert libraries, directories
                    # User stdin is an explicit lifetime barrier: the owner and
                    # loaded library are still alive until the test releases it.
                    assert owner.process.poll() is None and all(p.is_file() for p in libraries)
                    owner.continue_run()
                    assert owner.line() == '10'
                    assert not owner.end(23 if explicit_exit else 0)
                    assert owner.lines.get(timeout=5) is None
                    cleaner.wait(timeout=40)
                    assert owner.marker('cleaner.done') == '0'
                    assert not directories[0].exists() and archive.is_file()
        ok('module descriptor/archive auto/required: cleanup after owner termination, saved archive retained')

        exported = root / 'export.goat'
        exported.write_text('const f=func(n){return n+1;};println(f(8));', encoding='utf-8')
        saved = start('saved', ['--cc', compiler, '--save-library', exported])
        assert not saved.end()
        suffix = '.dll' if os.name == 'nt' else '.so'
        assert exported.with_suffix(suffix).stat().st_size > 0
        assert not list(saved.root.glob('goat-run-*'))
        compiled_source = root / 'compiled.goat'
        compiled_source.write_text(exported.read_text(), encoding='utf-8')
        compiled = start('compiled', ['--cc', compiler, '--compile', '--native', 'required', compiled_source])
        error = compiled.end()
        assert not error, error
        assert compiled_source.with_suffix('.gbin').stat().st_size > 0
        assert compiled_source.with_suffix(suffix).stat().st_size > 0
        assert not list(compiled.root.glob('goat-run-*'))
        ok('saved library and separated bytecode/native artifacts survive, temporary sources do not')

        a = start('interrupted', ['build', root / 'never-published.so'], GOAT_NATIVE_TEST_BLOCK='1')
        b = start('unrelated', ['build', root / 'other.so'], GOAT_NATIVE_TEST_BLOCK='1')
        a_cleaner = a.watch('cleaner.pid')
        a_compiler = a.watch('compiler.pid')
        a_worker = a.watch('worker.pid')
        b_cleaner = b.watch('cleaner.pid')
        b_compiler = b.watch('compiler.pid')
        b_worker = b.watch('worker.pid')
        output = Path(a.marker('compiler.output'))
        # The fixture writes UTF-8; Windows may spell the same directory with
        # a different drive case or extended-length prefix. Compare filesystem
        # identity, preserving the exact three-level containment requirement.
        assert output.parent.parent.parent.samefile(a.root), (str(output), str(a.root))
        assert (output.parent / 'module.c').is_file()
        assert a_cleaner.alive() and b_cleaner.alive()
        a.process.kill()
        a.process.wait(timeout=10)
        a_compiler.wait()
        a_worker.wait()
        a_cleaner.wait()
        assert not output.parent.parent.exists()
        assert not (root / 'never-published.so').exists()
        assert b_compiler.alive() and b_worker.alive() and b_cleaner.alive()
        b.process.kill()
        b.process.wait(timeout=10)
        b_compiler.wait()
        b_worker.wait()
        b_cleaner.wait()
        ok('owner death stops managed compiler and descendant before removing files, unrelated build untouched')
    finally:
        for owner in owners:
            owner.close()
print(f'Native cleanup testing done; total: {passed}, passed: {passed}, failed: 0')
