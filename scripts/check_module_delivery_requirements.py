#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Run a newer-built native module at its exact declared glibc floor, not the host's.

prepare-floor GOAT CC DEST runs on the ordinary Linux builder.
verify-floor GOAT DEST runs
inside the older userspace with a Goat built there. No source or compiler is
needed by the delivered artifact. A changed floor is a failure requiring a new
verified environment, never a successful skip or edited metadata.
"""
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
import zipfile


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=180, **kwargs)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    return result


def prepare(goat, compiler, destination):
    host = os.confstr('CS_GNU_LIBC_VERSION').split()
    assert host[0] == 'glibc' and tuple(map(int, host[1].split('.'))) > (2, 35), host
    destination.mkdir(parents=True, exist_ok=True)
    assert not any(destination.iterdir()), 'use a fresh delivery directory'
    compiler = shutil.which(compiler) or compiler
    with tempfile.TemporaryDirectory(prefix='goat-floor-producer-') as temporary:
        source = Path(temporary)
        (source / 'main.goat').write_text(
            'external square=func(x){return x*x;}; println(square(7));', encoding='utf-8')
        descriptor = source / 'module.gmod'
        descriptor.write_text(json.dumps(dict(formatVersion=1, name='runtime-floor',
                                              version='1', main='main.goat')), encoding='utf-8')
        result = run([goat, 'build', descriptor, '--native', 'required', '--cc', compiler])
        assert not result.stdout, result.stdout
        archive = source / 'modules/runtime-floor/1.gbin'
        with zipfile.ZipFile(archive) as container:
            metadata = json.loads(container.read('module.json'))
            assert len(metadata['native']) == 1
            variant = metadata['native'][0]
            target = variant['target']
            assert target['os'] == 'linux' and target['runtime']['family'] == 'glibc'
            assert target['runtime']['minimumVersion'] == [2, 35], (
                'The verified Ubuntu 22.04 floor is glibc 2.35; provide a matching real '
                'environment if compiler/dependency requirements change', target)
            assert hashlib.sha256(container.read(variant['library'])).hexdigest() == variant['hash']
        shutil.copyfile(archive, destination / 'module.gbin')
    assert sorted(p.name for p in destination.iterdir()) == ['module.gbin']
    print('Prepared unmodified native module from newer build environment: glibc minimum 2.35')


def verify(goat, destination):
    archive = destination / 'module.gbin'
    with zipfile.ZipFile(archive) as container:
        metadata = json.loads(container.read('module.json'))
    requirement = metadata['native'][0]['target']['runtime']['minimumVersion']
    actual = os.confstr('CS_GNU_LIBC_VERSION').split()
    assert actual[0] == 'glibc', actual
    version = list(map(int, actual[1].split('.')))
    assert (version + [0] * 4)[:4] == (requirement + [0] * 4)[:4], (
        'This must run at the declared minimum, not merely on a newer libc', version, requirement)
    before = archive.read_bytes()
    with tempfile.TemporaryDirectory(prefix='goat-floor-consumer-') as temporary:
        root = Path(temporary)
        delivered = root / 'module.gbin'
        delivered.write_bytes(before)
        env = dict(os.environ, PATH=str(root / 'no-compiler'), CC=str(root / 'absent'),
                   CXX=str(root / 'absent'), GOAT_MODULE_PATH='')
        for mode in ('off', 'auto', 'required'):
            report = root / (mode + '.native.txt')
            result = run([goat, delivered, '--native', mode, '--save-native', report],
                         cwd=root, env=env)
            assert result.stdout == b'49\n', (mode, result.stdout, result.stderr)
            fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
            assert int(fields['succeeded']) == 0 if mode == 'off' else int(fields['succeeded']) > 0, fields
            if mode != 'off':
                assert int(fields['bound']) > 0, fields
        assert delivered.read_bytes() == before
        assert not list(root.glob('*.goat')) and not list(root.glob('*.gmod'))
    print(f'Exact glibc {actual[1]} floor: VM, auto and required passed; real native calls confirmed')


def audit(goat, wrapper, selector, compiler):
    """Audit actual producer commands, then select the resulting immutable archive."""
    compiler_path = shutil.which(compiler)
    assert compiler_path, ('A real compiler is required', compiler)
    compiler = str(Path(compiler_path).resolve())
    wrapper, selector = str(Path(wrapper).resolve()), str(Path(selector).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix='goat-delivery-requirements-') as temporary:
        root = Path(temporary)
        source = root / 'main.goat'
        source.write_text('external square=func(x){return x*x;}; println(square(7));', encoding='utf-8')
        descriptor = root / 'module.gmod'
        descriptor.write_text(json.dumps(dict(formatVersion=1, name='requirements', version='1',
                                              main='main.goat')), encoding='utf-8')
        log = root / 'arguments.log'
        env = dict(os.environ, GOAT_TOOLCHAIN_REAL=compiler,
                   GOAT_TOOLCHAIN_ARGUMENTS_LOG=str(log),
                   CFLAGS='-march=native -mavx512f', CXXFLAGS='-mcpu=native')
        result = run([goat, 'build', descriptor, '--native', 'required', '--cc', wrapper], env=env)
        assert not result.stdout
        archive = root / 'modules/requirements/1.gbin'
        with zipfile.ZipFile(archive) as container:
            metadata = json.loads(container.read('module.json'))
        variant = metadata['native'][0]
        target = variant['target']
        architecture = target['architecture']
        baseline, features = {
            'x86': ('-march=i686', ['i686']),
            'x86_64': ('-march=x86-64', ['sse2']),
            'aarch64': ('-march=armv8-a', ['fp', 'neon'])}[architecture]
        assert target['cpuFeatures'] == features, target
        commands = [[word.decode('utf-8') for word in line.split(b'\0') if word]
                    for line in log.read_bytes().split(b'\n') if line]
        builds = [command for command in commands if './module.c' in command]
        assert builds, commands
        for command in builds:
            assert baseline in command and '-O2' in command, command
            assert not any('native' in flag for flag in command if flag.startswith(('-march=', '-mcpu='))), command
            assert not any(flag.startswith(('-mavx', '-msve')) for flag in command), command
            # Embedded runtime is rebuilt in the SAME invocation, not a host-optimized archive.
            for runtime in ('runtime_strings.c', 'runtime_str.c', 'runtime_number_format.c', 'runtime_unicode.c'):
                assert runtime in command, command
        flags = builds[0][:builds[0].index('-o')]
        macros = run([compiler, *flags, '-dM', '-E', '-x', 'c', '-'], input=b'').stdout.decode()
        if architecture == 'x86_64':
            assert '#define __SSE2__ 1' in macros
            forbidden = ('__AVX__', '__AVX2__', '__AVX512F__')
        elif architecture == 'aarch64':
            assert '#define __ARM_NEON 1' in macros
            forbidden = ('__ARM_FEATURE_SVE', '__ARM_FEATURE_SVE2', '__ARM_FEATURE_CRYPTO',
                         '__ARM_FEATURE_ATOMICS', '__ARM_FEATURE_AES', '__ARM_FEATURE_SHA2')
        else:
            assert '#define __i386__ 1' in macros
            forbidden = ('__AVX__', '__AVX2__', '__AVX512F__')
        assert not any('#define ' + feature + ' ' in macros for feature in forbidden), macros
        raw_archive = archive.read_bytes()

        def selected(profile, expected, abis=(1,)):
            nonlocal count
            raw = json.dumps(profile).encode('utf-8')
            framed = (struct.pack('<I', len(raw)) + raw + struct.pack('<I', len(abis))
                      + b''.join(struct.pack('<I', abi) for abi in abis) + raw_archive)
            result = run([selector, 'select'], input=framed)
            report = json.loads(result.stdout)
            assert report.get('status') == expected, (profile, abis, report)
            count += 1

        selected(target, 0)
        for abis in ((), (0,), (2,), (4294967295,)):
            selected(target, 3, abis)
        profile = copy.deepcopy(target)
        profile['cpuFeatures'] = []
        selected(profile, 3)
        # Real produced library, same bitness but a different architecture.
        profile = copy.deepcopy(target)
        if architecture != 'x86':
            profile['architecture'] = 'aarch64' if architecture == 'x86_64' else 'x86_64'
            profile['cpuFeatures'] = ['fp', 'neon'] if architecture == 'x86_64' else ['sse2']
            selected(profile, 3)
        if target['os'] == 'linux':
            profile = copy.deepcopy(target)
            profile['runtime']['family'] = 'musl'
            selected(profile, 3)
        else:
            profile = copy.deepcopy(target)
            if architecture == 'x86':
                profile.update(architecture='x86_64', bits=64, cpuFeatures=['sse2'])
            else:
                profile.update(architecture='x86', bits=32, cpuFeatures=['i686'])
            selected(profile, 3)
            if architecture == 'x86_64':
                profile = copy.deepcopy(target)
                profile['runtime']['family'] = 'ucrt' if target['runtime']['family'] == 'msvcrt' else 'msvcrt'
                selected(profile, 3)
        for field in ('minimumOs', 'runtime'):
            minimum = target[field]['minimumVersion'] if field == 'runtime' else target[field]
            for increment in (-1, 0, 1):
                value = list(minimum)
                nonzero = next((i for i in reversed(range(len(value))) if value[i]), None)
                if increment < 0 and nonzero is None:
                    continue  # The explicit no-extra-floor [0] has no smaller valid version.
                value[nonzero if increment < 0 else len(value) - 1] += increment
                profile = copy.deepcopy(target)
                if field == 'runtime':
                    profile[field]['minimumVersion'] = value
                else:
                    profile[field] = value
                # Windows 9 is outside the supported profile schema, tested by existing suite.
                if field == 'minimumOs' and target['os'] == 'windows' and value[0] < 10:
                    continue
                selected(profile, 3 if increment < 0 else 0)
        host = run([selector, 'host']).stdout
        for fake in ('MINGW32', 'MINGW64', 'UCRT64'):
            assert run([selector, 'host'], env=dict(os.environ, MSYSTEM=fake, PATH=str(root))).stdout == host
        if architecture in ('x86_64', 'aarch64'):
            extension = 'avx2' if architecture == 'x86_64' else 'sha2'
            # An explicitly requested extension changes the effective whole-image flags
            # and must not be selected on a baseline-only process.
            extended_dir = root / 'extended'
            extended_dir.mkdir()
            run([goat, 'build', descriptor, '--native', 'required', '--native-target', 'host+' + extension,
                 '--cc', wrapper, '--module-dir', extended_dir], env=env)
            extended = extended_dir / 'requirements/1.gbin'
            with zipfile.ZipFile(extended) as container:
                extended_metadata = json.loads(container.read('module.json'))
            required_features = ['avx2'] if extension == 'avx2' else ['sha1', 'sha2']
            assert all(feature in extended_metadata['native'][0]['target']['cpuFeatures'] for feature in required_features)
            extension_flag = b'-mavx2' if extension == 'avx2' else b'-march=armv8-a+sha2'
            extended_commands = [line.split(b'\0') for line in log.read_bytes().split(b'\n')
                                 if b'./module.c\0' in line and extension_flag + b'\0' in line]
            assert extended_commands, 'explicit extension must apply to generated code and runtime'
            for command in extended_commands:
                for runtime in (b'runtime_strings.c', b'runtime_str.c', b'runtime_number_format.c', b'runtime_unicode.c'):
                    assert runtime in command
                extension_flags = [word.decode('utf-8') for word in command[:command.index(b'-o')]]
                extension_macros = run([compiler, *extension_flags, '-dM', '-E', '-x', 'c', '-'], input=b'').stdout
                required_macros = ['__AVX2__'] if extension == 'avx2' else [
                    '__ARM_FEATURE_SHA2']
                assert all(('#define ' + macro + ' 1').encode() in extension_macros for macro in required_macros), extension_macros
                if extension == 'sha2':
                    assert b'#define __ARM_FEATURE_AES ' not in extension_macros
                    assert b'#define __ARM_FEATURE_CRYPTO ' not in extension_macros
            raw_archive = extended.read_bytes()
            selected(target, 3)
    print(f'Delivery requirements: {count} real-archive selection checks; effective compiler/runtime flags audited')


if __name__ == '__main__':
    mode, executable, *arguments = sys.argv[1:]
    executable = str(Path(executable).resolve())
    if mode == 'prepare-floor':
        prepare(executable, arguments[0], Path(arguments[1]).resolve())
    elif mode == 'verify-floor':
        verify(executable, Path(arguments[0]).resolve())
    else:
        assert mode == 'audit'
        audit(executable, *arguments)
