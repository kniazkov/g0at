#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Launch-bound constants across source files, imports and relocated archives (#206)."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from module_execution_support import ExecutionSuite, put


def directory(path):
    return Path(path).resolve().parent.as_posix()


def symlink(link, target):
    try:
        link.symlink_to(target)
    except OSError as error:
        if os.name != 'nt':
            raise
        print('Windows symlink fixture unavailable:', error)
        return False
    return True


with tempfile.TemporaryDirectory(prefix='goat-context-constants-') as temporary:
    suite = ExecutionSuite(sys.argv[1], temporary)

    # Exactly the same bytes are directly executable and importable. Both branches
    # must remain; the library directory must never leak into a consumer's __dir__.
    p = suite.project('same-image')
    library = p.compile('library',
        'external main = func() { return __main__; }; '
        'external dir = func() { return __dir__; }; '
        'if (__main__) { println("direct"); } else { println("imported"); } '
        'println(__dir__);')
    original = library.read_bytes()
    p.invoke('binary', library, expected='direct\n' + directory(library) + '\n')
    p.run('const lib = import "library@1"; println(__main__); println(lib.main()); '
          'println(lib.dir() == __dir__); println(__dir__);',
          'imported\n' + p.root.resolve().as_posix() + '\ntrue\nfalse\ntrue\n' +
          p.root.resolve().as_posix() + '\n')
    assert library.read_bytes() == original

    # Declaration identity survives call direction, escaped closures, receiver
    # storage, cloning and flattening. Includes have their own false identity.
    p = suite.project('file-origin')
    p.run('const other = include "nested/other.goat"; '
          'const read = func() { return __main__; }; '
          'const make = func() { return func() { return __main__; }; }; '
          'println(__main__); println(other.main); println(other.dir == __dir__); '
          'println(other.call(read)); println(other.call(make())); '
          'println(other.read()); println(other.make()()); '
          'const holder = { public local = read; public foreign = other.read; }; '
          'println(holder.local()); println(holder.foreign()); '
          'println(holder.clone().local()); println(holder.clone().foreign()); '
          'println(holder.flat().local()); println(holder.flat().foreign()); '
          'println(read.clone()()); println(other.read.clone()());',
          'true\nfalse\ntrue\ntrue\ntrue\nfalse\nfalse\ntrue\nfalse\n'
          'true\nfalse\ntrue\nfalse\ntrue\nfalse\n', files={
              'nested/other.goat': 'public main = __main__; public dir = __dir__; '
                  'public call = func(f) { return f(); }; '
                  'public read = func() { return __main__; }; '
                  'public make = func() { return func() { return __main__; }; };'})

    p = suite.project('reexport')
    p.compile('library', 'external read = func() { return __main__; }; '
              'external make = func() { return func() { return __main__; }; }; '
              'external call = func(f) { return f(); }; '
              'external dir = func() { return __dir__; };')
    p.compile('facade', 'const lib = import "library@1"; external read = lib.read; '
              'external make = lib.make; external call = lib.call; external dir = lib.dir;')
    p.run('const f = import "facade@1"; println(f.read()); println(f.make()()); '
          'println(f.call(func() { return __main__; })); println(f.dir() == __dir__); '
          'const holder = { public read = f.read; }; '
          'println(holder.clone().read()); println(holder.flat().read());',
          'false\nfalse\ntrue\ntrue\nfalse\nfalse\n')

    # No build-machine directory in the image or metadata; moving an unchanged
    # artifact changes its launch directory without recompilation.
    p = suite.project('relocation')
    archive = p.compile('app', 'println(__main__); println(__dir__);')
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            content = z.read(name)
            for spelling in [str(p.root), str(p.root).replace('\\', '/')]:
                assert spelling.encode() not in content, name
                assert spelling.encode('utf-32-le') not in content, name
    p.invoke('binary', archive, expected='true\n' + directory(archive) + '\n')
    moved = Path(temporary) / 'moved' / 'app.gbin'
    moved.parent.mkdir()
    shutil.move(archive, moved)
    shutil.rmtree(p.root / 'sources')
    p.invoke('binary', moved, expected='true\n' + directory(moved) + '\n')

    # UTF-8 launch paths survive the filesystem boundary without locale-dependent
    # conversion (including a supplementary-plane character).
    p = suite.project('каталог-λ-🦉')
    unicode_archive = p.compile('app', 'println(__main__); println(__dir__);')
    p.invoke('binary', unicode_archive,
             expected='true\n' + directory(unicode_archive) + '\n')

    # Resolve launch aliases once, including local dependency discovery. A broken
    # module beside the alias must not supersede the real program's dependency.
    p = suite.project('canonical')
    p.compile('library', 'external value = 42; external dir = __dir__;')
    archive = p.compile('app', 'const lib = import "library@1"; println(lib.value); '
                        'println(__dir__); println(lib.dir == __dir__);')
    launch = Path(temporary) / 'links'
    launch.mkdir()
    put(launch / 'modules/library/1.gbin', 'invalid alias-side dependency')
    alias = launch / 'app.gbin'
    expected = '42\n' + directory(archive) + '\ntrue\n'
    p.invoke('binary', archive, expected=expected)
    alternate = suite.project('replacement')
    replacement = alternate.compile('app', 'println("wrong replacement");')
    frozen_paths = [archive]
    if symlink(alias, archive):
        p.invoke('binary', alias, expected=expected)
        frozen_paths.append(alias)
    for frozen_path in frozen_paths:
        command = [suite.exe, 'frozen-chdir', str(frozen_path), str(alternate.root)]
        process = subprocess.Popen(command, cwd=suite.unrelated, env=p.environment,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        try:
            assert process.stdout.readline().strip() == b'READY', command
            if frozen_path == alias:
                alias.unlink()
                alias.symlink_to(replacement)
            stdout, stderr = process.communicate(b'continue\n', timeout=60)
            suite.count += 1
            assert process.returncode == 0, (command, stderr)
            assert stdout.decode('utf-8').replace('\r\n', '\n') == expected, (stdout, expected)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    # Both names are reserved throughout declarations and lexical assignments,
    # not just at module top level or in reachable branches.
    for name in ['__main__', '__dir__']:
        invalid = [f'{name} = 1;', f'{name} += 1;', f'var {name} = 1;',
                   f'const {name} = 1;', f'public {name} = 1;',
                   f'external {name} = 1;',
                   f'const f = func() {{ var {name} = 1; }};',
                   f'const f = func({name}) {{ return 1; }};',
                   f'const f = func() {{ {name} = 1; }};',
                   f'if (false) {{ const {name} = 1; }};',
                   f'try {{ throw 1; }} catch ({name}) {{ }};',
                   f'for (const {name} in [1]) {{ }};']
        for index, code in enumerate(invalid):
            p = suite.project(f'reserved-{name}-{index}')
            p.compile('app', code, fail=True)

    # Source launch and optimization/native selection use the same runtime values.
    # Unsupported native bodies must fall back, never substitute a build-time value.
    if len(sys.argv) > 2:
        goat = str(Path(sys.argv[2]).resolve())
        source = put(Path(temporary) / 'исходник-λ-🦉' / 'main.goat',
                     'const read = func() { if (__main__) return 1; else return 2; }; '
                     'const dir = func() { return __dir__; }; '
                     'println(__main__); println(read()); println(dir()); '
                     'println(read.clone()());')
        expected = 'true\n1\n' + directory(source) + '\n1\n'
        source_alias = launch / 'main.goat'
        paths = [source]
        if symlink(source_alias, source):
            paths.append(source_alias)
        for path in paths:
            for optimization, native in [('none', 'off'), ('all', 'off'), ('all', 'auto')]:
                command = [goat, '--optimize', optimization, '--native', native, str(path)]
                result = subprocess.run(command, cwd=suite.unrelated, capture_output=True, timeout=60)
                suite.count += 1
                assert result.returncode == 0, (command, result.stderr)
                actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
                assert actual == expected, (command, actual, expected, result.stderr)


        # Native preparation is an external operation between reading the source
        # and executing it. Retarget the original link from a fake compiler, then
        # force auto's ordinary VM fallback. The launch directory must already be
        # fixed, not canonicalized again after the compiler returns.
        original_source = put(Path(temporary) / 'before-native' / 'main.goat',
            'const inc = func(x) { return x + 1; }; println(inc(7)); '
            'println(__main__); println(__dir__);')
        replacement_source = put(Path(temporary) / 'after-native' / 'main.goat',
                                 'println("wrong replacement source");')
        native_alias = launch / 'native.goat'
        swapped_alias = launch / 'swapped.goat'
        if symlink(native_alias, original_source):
            swapped_alias.symlink_to(replacement_source)
            compiler_source = put(Path(temporary) / 'compiler.c', r'''#include <stdio.h>
#include <stdlib.h>
int main(void) {
    const char *alias = getenv("GOAT_TEST_ALIAS");
    const char *swapped = getenv("GOAT_TEST_SWAPPED");
    const char *marker = getenv("GOAT_TEST_MARKER");
    if (!alias || !swapped || !marker || remove(alias) || rename(swapped, alias))
        return 2;
    FILE *file = fopen(marker, "wb");
    if (!file) return 3;
    fputs("retargeted", file);
    fclose(file);
    return 1;
}
''')
            compiler = Path(temporary) / ('compiler.exe' if os.name == 'nt' else 'compiler')
            command = [os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       str(compiler_source), '-o', str(compiler)]
            built = subprocess.run(command, capture_output=True, timeout=60)
            assert built.returncode == 0, (command, built.stderr)
            marker = Path(temporary) / 'compiler-called'
            environment = dict(os.environ, CC=str(compiler), GOAT_TEST_ALIAS=str(native_alias),
                               GOAT_TEST_SWAPPED=str(swapped_alias), GOAT_TEST_MARKER=str(marker))
            command = [goat, '--optimize', 'all', '--native', 'auto', str(native_alias)]
            result = subprocess.run(command, env=environment, cwd=suite.unrelated,
                                    capture_output=True, timeout=60)
            suite.count += 1
            assert marker.read_text() == 'retargeted'
            assert native_alias.resolve() == replacement_source.resolve()
            expected = '8\ntrue\n' + directory(original_source) + '\n'
            actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
            assert result.returncode == 0 and actual == expected, (
                command, result.returncode, actual, expected, result.stderr)

print(f'Module context constant checks passed: {suite.count}')
