#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Public CLI integration for module production, validated execution and toolchains."""
import errno
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

from module_execution_support import put
from process_test_support import until

GOAT = str(Path(sys.argv[1]).resolve())
WRAPPER = Path(sys.argv[2]).resolve()
CC = shutil.which(sys.argv[3] if len(sys.argv) > 3 else os.environ.get('CC', 'gcc'))
assert CC, 'A real compiler is required for CLI integration checks'
CC = str(Path(CC).resolve())
count = 0
unsupported_replacements = 0
ALLOW_UNSUPPORTED_ACL = '--allow-unsupported-acl' in sys.argv[4:]


def content(path):
    with zipfile.ZipFile(path) as archive:
        return json.loads(archive.read('module.json')), {
            name: archive.read(name) for name in archive.namelist()}


def output(result):
    return result.stdout.decode('utf-8').replace('\r\n', '\n')


with tempfile.TemporaryDirectory(prefix='goat-module-cli-') as temporary:
    root = Path(temporary)
    cwd = root / 'unrelated'
    cwd.mkdir()
    base_env = dict(os.environ, GOAT_MODULE_PATH='')

    def invoke(*args, expected=None, fail=False, env=None, directory=cwd, timeout=180):
        global count, unsupported_replacements
        current = dict(base_env)
        current.update(env or {})
        command = [GOAT, *map(str, args)]
        result = subprocess.run(command, cwd=directory, env=current,
                                capture_output=True, timeout=timeout)
        # The cloud filesystem may not implement ACL reads. This opt-in local-only
        # rerun verifies fresh publication, not replacement. CI never passes the flag.
        prefix = b'Module publication failed (8): '
        publication = next((line for line in result.stderr.splitlines() if line.startswith(prefix)), None)
        if ALLOW_UNSUPPORTED_ACL and not fail and result.returncode and publication:
            destination = Path(publication[len(prefix):].decode('utf-8').strip())
            assert destination.is_file(), (destination, result.stderr)
            try:
                os.getxattr(destination, 'system.posix_acl_access')
            except OSError as error:
                assert error.errno in (errno.ENOTSUP, errno.EOPNOTSUPP), error
            else:
                raise AssertionError('replacement failed although ACL reads are supported')
            destination.unlink()
            unsupported_replacements += 1
            result = subprocess.run(command, cwd=directory, env=current,
                                    capture_output=True, timeout=timeout)
        assert (result.returncode != 0) if fail else result.returncode == 0, (
            command, result.returncode, result.stdout, result.stderr)
        if expected is not None:
            assert output(result) == expected, (command, output(result), expected, result.stderr)
        if fail:
            assert result.stderr or result.stdout, ('failure needs a diagnostic', command)
        count += 1
        return result

    def project(directory, name, source, version='1', **fields):
        descriptor = dict(formatVersion=1, name=name, version=version, main='main.goat')
        descriptor.update(fields)
        put(directory / 'main.goat', source)
        return put(directory / 'module.gmod', json.dumps(descriptor, ensure_ascii=False))

    def artifact(descriptor, module_dir=None):
        doc = json.loads(descriptor.read_text(encoding='utf-8'))
        return (module_dir or descriptor.resolve().parent / 'modules') / doc['name'] / (doc['version'] + '.gbin')

    def build(descriptor, module_dir=None, mode='off', **kwargs):
        args = ['build', descriptor, '--native', mode]
        if module_dir is not None:
            args += ['--module-dir', module_dir]
        kwargs.setdefault('expected', '')
        result = invoke(*args, **kwargs)
        return result, artifact(descriptor, module_dir)

    # Source discovery failures must happen before any entry effect.
    missing_include = put(root / 'missing-include.goat', 'println("must not run"); include "absent.goat";')
    invoke(missing_include, '--native', 'off', fail=True, expected='')

    # Public build publishes a module ZIP but must never execute its initializer.
    basic = project(root / 'basic', 'basic', 'println("built only when run");',
                    authors=[{'name': 'Author', 'email': 'not an email'}],
                    homepage='not a URL')
    _, binary = build(basic)
    invoke(basic, '--optimize', 'none', '--native', 'auto', expected='built only when run\n')
    metadata, entries = content(binary)
    assert metadata['name'] == 'basic' and metadata['version'] == '1'
    assert not metadata.get('native') and 'bytecode.bin' in entries
    assert metadata['authors'][0]['email'] == 'not an email'
    producers = metadata['producers']
    assert len(producers) == 1 and all(producers[0].get(key) for key in ('name', 'version', 'buildId'))
    assert not any(str(root) in value or '\\' in value for value in producers[0].values())
    previous = binary.read_bytes()
    build(basic)
    assert binary.read_bytes() == previous, 'identical CLI builds must be byte deterministic'
    invoke(binary, '--native', 'off', expected='built only when run\n')
    invoke('run', binary, '--native', 'off', expected='built only when run\n')
    invoke('--run', binary, '--native', 'off', expected='built only when run\n')
    assert binary.read_bytes() == previous, 'running cannot rewrite ready bytecode'

    # Source-independent executable archive and canonical launch context.
    put(basic.parent / 'main.goat', 'this is not valid Goat source !!!')
    invoke(binary, '--native', 'off', expected='built only when run\n')
    build(basic, fail=True)
    assert binary.read_bytes() == previous, 'failed source compilation must preserve old output'
    put(basic.parent / 'main.goat', 'println("replacement");')
    build(basic)
    assert binary.read_bytes() != previous
    invoke(binary, '--native', 'off', expected='replacement\n')
    assert sorted(p.name for p in binary.parent.iterdir()) == ['1.gbin']

    # Every descriptor is validated before CLI overrides, even unused settings.
    valid = dict(formatVersion=1, name='strict', version='1', main='main.goat')
    strict_dir = root / 'strict'
    put(strict_dir / 'main.goat', 'println("never during build");')
    strict = strict_dir / 'module.gmod'
    invalid = [
        dict(native={'mode': 'atuo'}), dict(native={'target': 42}),
        dict(native={'mode': 'off', 'unknown': True}), dict(unknown=True),
        dict(dependencies={'bad name': '1'}), dict(dependencies={'unused': []}),
        dict(dependencies={'unused': '../1'}), dict(authors=[{'name': 'ok', 'extra': 1}]),
        dict(version=None), dict(main=False),
    ]
    for values in invalid:
        put(strict, json.dumps(dict(valid, **values)))
        invoke('build', strict, '--native', 'off', '--native-target', 'host', fail=True)
        assert not (strict_dir / 'modules').exists()
    put(strict, '{"formatVersion":1,"name":"strict","n\\u0061me":"strict",'
                '"version":"1","main":"main.goat"}')
    invoke('build', strict, '--native', 'off', fail=True)
    assert not (strict_dir / 'modules').exists()
    put(strict, json.dumps(dict(valid, native={'mode': 'required', 'target': 'linux-unknown-gnu'},
                               dependencies={'unused': '1'})))
    no_compiler = root / 'missing-compiler'
    result = invoke('build', strict, '--native', 'off', '--cc', no_compiler)
    assert 'unused' in result.stderr.decode('utf-8').lower(), result.stderr
    assert not content(artifact(strict))[0].get('native')

    # Native settings really override valid descriptors independently.
    numeric = project(root / 'numeric', 'numeric',
                      'external square=func(x){return x*x;}; println(square(7));',
                      native={'mode': 'required', 'target': 'host'})
    invoke('build', numeric, '--native', 'off', '--cc', no_compiler)
    numeric_binary = artifact(numeric)
    off_doc, off_entries = content(numeric_binary)
    invoke(numeric_binary, '--native', 'off', expected='49\n')
    invoke(numeric_binary, '--native', 'required', fail=True, expected='')
    prior = numeric_binary.read_bytes()
    invoke('build', numeric, '--native', 'required', '--cc', no_compiler, fail=True)
    assert numeric_binary.read_bytes() == prior
    invoke('build', numeric, '--native', 'auto', '--native-target', 'linux-unknown-gnu',
           '--cc', no_compiler, fail=True)
    assert numeric_binary.read_bytes() == prior
    auto = invoke('build', numeric, '--native', 'auto', '--cc', no_compiler)
    assert b'warning' in auto.stderr.lower(), auto.stderr
    assert not content(numeric_binary)[0].get('native')
    invoke('build', numeric, '--native', 'required', '--cc', CC, expected='')
    native_doc, native_entries = content(numeric_binary)
    assert len(native_doc['native']) == 1
    assert native_doc['mainHash'] == off_doc['mainHash']
    assert native_entries['bytecode.bin'] == off_entries['bytecode.bin']
    assert native_doc['producers'] == off_doc['producers']
    invoke(numeric_binary, '--native', 'required', expected='49\n')
    invoke(numeric_binary, '--native', 'auto', expected='49\n')
    invoke(numeric_binary, '--native', 'off', expected='49\n')

    # Full container validation precedes all code, even when native is disabled.
    damaged = root / 'damaged.gbin'
    library_name = native_doc['native'][0]['library']
    damaged_entries = dict(native_entries)
    damaged_entries[library_name] += b'corruption'
    with zipfile.ZipFile(damaged, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, data in damaged_entries.items():
            archive.writestr(name, data)
    invoke(damaged, '--native', 'off', fail=True, expected='')

    # Relative output is rooted at the startup cwd, not the description directory.
    real_project = root / 'real' / 'проект 漢字'
    app = project(real_project, 'app', 'println((import "value@1").value);')
    local_modules = real_project / 'modules'
    explicit_modules = cwd / 'output'
    env_modules = cwd / 'vendor'
    for repository, value, label in ((local_modules, 11, 'local'),
                                     (explicit_modules, 22, 'explicit'),
                                     (env_modules, 33, 'environment')):
        dependency = project(root / ('dependency-' + label), 'value', f'external value={value};')
        build(dependency, repository)
    env = {'GOAT_MODULE_PATH': 'vendor' + os.pathsep + os.pathsep + 'vendor'}
    invoke('build', app, '--native', 'off', '--module-dir', './output', env=env)
    app_binary = explicit_modules / 'app' / '1.gbin'
    first_doc = content(app_binary)[0]
    assert first_doc['dependencies'][0]['hash'] == content(local_modules / 'value/1.gbin')[0]['mainHash']
    # A relocated executable sees modules beside the executable, not an inferred repo root.
    distribution = root / 'distribution'
    distribution.mkdir()
    portable = distribution / 'app.gbin'
    shutil.copyfile(app_binary, portable)
    shutil.copytree(local_modules / 'value', distribution / 'modules' / 'value')
    invoke(portable, '--native', 'off', '--module-dir', explicit_modules, expected='11\n')
    # The first found wrong build is an error, never a fallback to a later matching copy.
    invoke(app_binary, '--native', 'off', '--module-dir', explicit_modules,
           env={'GOAT_MODULE_PATH': str(local_modules)}, fail=True, expected='')
    (local_modules / 'value/1.gbin').unlink()
    invoke('build', app, '--native', 'off', '--module-dir', './output', env=env)
    assert content(app_binary)[0]['dependencies'][0]['hash'] == content(explicit_modules / 'value/1.gbin')[0]['mainHash']
    invoke(app_binary, '--native', 'off', '--module-dir', './output', env=env, expected='22\n')
    (explicit_modules / 'value/1.gbin').unlink()
    invoke('build', app, '--native', 'off', '--module-dir', './output', env=env)
    invoke(app_binary, '--native', 'off', '--module-dir', './output', env=env, expected='33\n')
    # Corruption in the first location cannot silently select a later archive.
    put(local_modules / 'value/1.gbin', 'corrupt archive')
    old = app_binary.read_bytes()
    invoke('build', app, '--native', 'off', '--module-dir', './output', env=env, fail=True)
    assert app_binary.read_bytes() == old

    # The prospective named root participates in graph validation before replacing it.
    cycle_root = root / 'cycle'
    cycle_modules = cycle_root / 'modules'
    cycle_app = project(cycle_root / 'app', 'cycleapp', 'external seed=1;')
    _, saved_app = build(cycle_app, cycle_modules)
    old_app = saved_app.read_bytes()
    cycle_dep = project(cycle_root / 'dep', 'cycledep',
                        'external seed=(import "cycleapp@1").seed;')
    build(cycle_dep, cycle_modules)
    put(cycle_app.parent / 'main.goat', 'external seed=(import "cycledep@1").seed;')
    build(cycle_app, cycle_modules, fail=True)
    assert saved_app.read_bytes() == old_app

    # Descriptor links use the real descriptor's base for main and default output.
    linked_real = root / 'link-real'
    linked = project(linked_real, 'linked', 'println("real source"); println((import "linkdep@1").value);')
    for place, value, label in ((linked_real, 41, 'real'), (root / 'link-alias', 99, 'alias')):
        dep = project(root / ('linkdep-' + label), 'linkdep', f'external value={value};')
        build(dep, place / 'modules')
    link_dir = root / 'link-alias'
    put(link_dir / 'main.goat', 'println("WRONG source");')
    alias = link_dir / 'alias.gmod'
    try:
        alias.symlink_to(linked)
    except OSError as error:
        if os.name != 'nt':
            raise
        print('Windows symlink fixture unavailable:', error)
    else:
        invoke('build', alias, '--native', 'off', directory=cwd)
        assert artifact(linked).is_file() and not (link_dir / 'modules/linked').exists()
        before = artifact(linked).read_bytes()
        invoke('build', linked, '--native', 'off', directory=root)
        assert artifact(linked).read_bytes() == before
        invoke('build', alias, '--native', 'off', '--module-dir', './link-output')
        link_output = cwd / 'link-output/linked/1.gbin'
        assert link_output.read_bytes() == before
        invoke(link_output, '--native', 'off', '--module-dir', linked_real / 'modules', expected='real source\n41\n')

    # Direct sources with include/import use the same validated graph pipeline.
    direct = put(root / 'direct' / 'main.goat',
                 'println(__main__); println((include "helper.goat").value); '
                 'println((import "value@1").value);')
    put(direct.parent / 'helper.goat', 'public value=40;')
    invoke(direct, '--native', 'off', '--module-dir', env_modules, expected='true\n40\n33\n')
    invoke('run', direct, '--native', 'off', '--module-dir', env_modules,
           expected='true\n40\n33\n')
    # Running a descriptor preserves the actual entry source's launch directory.
    context = project(root / 'run-description', 'context',
                      'println(__main__); println(__dir__);')
    expected_context = 'true\n' + context.parent.resolve().as_posix() + '\n'
    invoke(context, '--native', 'off', expected=expected_context)
    invoke('run', context, '--native', 'off', expected=expected_context)
    assert not (context.parent / 'modules').exists(), 'run .gmod must not publish an artifact'
    invoke('--compile', direct, '--native', 'off', '--module-dir', env_modules, expected='')
    invoke('--run', direct.with_suffix('.gbin'), '--native', 'off',
           '--module-dir', env_modules, expected='true\n40\n33\n')

    # Target enumeration probes the selected executable, not merely --version.
    suffix = '.exe' if os.name == 'nt' else ''
    compiler = root / ('controlled-compiler' + suffix)
    shutil.copy2(WRAPPER, compiler)
    calls = root / 'compiler.log'
    controlled = {'GOAT_TOOLCHAIN_REAL': CC, 'GOAT_TOOLCHAIN_LOG': str(calls)}
    temporary_root = root / 'probe-temporary'
    temporary_root.mkdir()
    controlled.update({key: str(temporary_root) for key in ('TMPDIR', 'TEMP', 'TMP')})
    listing = invoke('--list-native-targets', '--cc', compiler, '--lang', 'en', env=controlled)
    until(lambda: not list(temporary_root.iterdir()))
    assert 'automatic' in output(listing).lower(), output(listing)
    assert compiler.as_posix().lower() in output(listing).replace('\\', '/').lower(), output(listing)
    assert calls.exists() and calls.read_text(encoding='utf-8').strip()
    first_calls = calls.read_bytes()
    invoke('--list-native-targets', '--cc', compiler, env=controlled)
    assert len(calls.read_bytes()) > len(first_calls), 'a new process must reprobe'
    invoke('--list-native-targets', '--cc', compiler,
           env=dict(controlled, GOAT_TOOLCHAIN_MODE='link-fail'), fail=True)
    invoke('--list-native-targets', '--cc', no_compiler, fail=True)
    invoke('--list-native-targets', env={'PATH': str(root / 'empty-path')}, fail=True)
    invoke('--list-native-targets', '--cc', compiler, '--native-probe-timeout', '1',
           env=dict(controlled, GOAT_TOOLCHAIN_MODE='hang-query'), fail=True, timeout=30)
    for invalid_timeout in ('0', '-1', '1.5', '184467440737095516160'):
        invoke('--list-native-targets', '--native-probe-timeout', invalid_timeout, fail=True)

    # Ordinary automatic selection stops early; enumeration shows successes and failures.
    tool_dir = root / 'tool-path'
    tool_dir.mkdir()
    for name in ('cc', 'gcc', 'clang'):
        shutil.copy2(WRAPPER, tool_dir / (name + suffix))
    for tool in ('as', 'ld'):
        location = shutil.which(tool)
        if location and os.name != 'nt':
            (tool_dir / tool).symlink_to(Path(location).resolve())
    preferred = 'gcc' if os.name == 'nt' else 'cc'
    fallback = 'clang' if os.name == 'nt' else 'gcc'
    tool_paths = os.pathsep.join(filter(None, (str(Path(CC).parent),
                                             str(Path(shutil.which('ld')).parent) if shutil.which('ld') else '')))
    isolated = dict(controlled, PATH=str(tool_dir) +
                    (os.pathsep + os.environ.get('PATH', '') if os.name == 'nt' else ''),
                    COMPILER_PATH=tool_paths)
    calls.write_bytes(b'')
    invoke('build', numeric, '--native', 'required', env=isolated)
    lines = calls.read_text(encoding='utf-8').splitlines()
    assert lines and all(preferred + suffix + ' ' in line for line in lines), lines
    calls.write_bytes(b'')
    selected = invoke('build', numeric, '--native', 'required',
                      env=dict(isolated, GOAT_TOOLCHAIN_FAIL=preferred + suffix))
    assert b'controlled compiler failure' not in selected.stderr, selected.stderr
    assert any(fallback + suffix + ' ' in line for line in calls.read_text(encoding='utf-8').splitlines())
    mixed = invoke('--list-native-targets', '--lang', 'en',
                   env=dict(isolated, GOAT_TOOLCHAIN_FAIL=preferred + suffix))
    assert b'controlled compiler failure' in mixed.stdout + mixed.stderr
    assert (tool_dir / (fallback + suffix)).as_posix().lower() in output(mixed).replace('\\', '/').lower(), output(mixed)
    calls.write_bytes(b'')
    invoke('build', numeric, '--native', 'required', '--cc', tool_dir / (preferred + suffix),
           env=dict(isolated, GOAT_TOOLCHAIN_FAIL=preferred + suffix), fail=True)
    assert all(preferred + suffix + ' ' in line for line in calls.read_text(encoding='utf-8').splitlines())

    # Off must not even probe the explicitly selected executable.
    calls.write_bytes(b'')
    invoke('build', numeric, '--native', 'off', '--cc', compiler, env=controlled)
    assert not calls.read_bytes()
    # Same wrapper observes environment during real probing and compilation.
    invoke('build', numeric, '--native', 'required', '--cc', compiler, env=controlled)
    assert calls.read_bytes()
    invoke(numeric_binary, '--native', 'required', expected='49\n')
    until(lambda: not list(temporary_root.iterdir()))

    # Descriptor mode applies when not overridden; absent mode defaults to auto.
    descriptor_off = project(root / 'descriptor-off', 'descriptor-off',
                             'external f=func(x){return x+1;}; f(2);', native={'mode': 'off'})
    calls.write_bytes(b'')
    invoke('build', descriptor_off, '--cc', compiler, env=controlled, expected='')
    assert not calls.read_bytes() and not content(artifact(descriptor_off))[0].get('native')
    default_auto = project(root / 'default-auto', 'default-auto',
                           'external f=func(x){return x+1;}; println(f(2));')
    invoke('build', default_auto, '--cc', compiler, env=controlled, expected='')
    assert content(artifact(default_auto))[0].get('native') and calls.read_bytes()
    invoke(artifact(default_auto), '--native', 'required', expected='3\n')
    until(lambda: not list(temporary_root.iterdir()))

    # Dependency native code does not satisfy direct source required mode.
    only_import = put(root / 'only-import.goat',
                      'println((import "default-auto@1").f(8));')
    invoke(only_import, '--native', 'required', '--module-dir', default_auto.parent / 'modules',
           '--cc', CC, fail=True, expected='')
    invoke(only_import, '--native', 'auto', '--module-dir', default_auto.parent / 'modules',
           '--cc', CC, expected='3\n9\n')

    # Reports reflect the effective default auto mode and cannot clobber inputs.
    report = root / 'native-report.txt'
    invoke(artifact(default_auto), '--save-native', report, expected='3\n')
    fields = dict(line.split('=', 1) for line in report.read_text(encoding='utf-8').splitlines())
    assert int(fields['succeeded']) > 0 and int(fields['bound']) > 0, fields
    saved = artifact(default_auto).read_bytes()
    invoke(artifact(default_auto), '--save-native', artifact(default_auto), fail=True, expected='')
    assert artifact(default_auto).read_bytes() == saved
    source = default_auto.parent / 'main.goat'
    saved_source = source.read_bytes()
    invoke(default_auto, '--native', 'off', '--save-native', source, fail=True, expected='')
    assert source.read_bytes() == saved_source
    # Case-insensitive known extensions select the same public pipelines.
    # Use a distinct basename: merely changing case aliases the source on Windows.
    upper = default_auto.with_name('uppercase-description.GMOD')
    shutil.copyfile(default_auto, upper)
    invoke('run', upper, '--native', 'off', expected='3\n')
    upper_binary = root / 'upper.GBIN'
    shutil.copyfile(artifact(default_auto), upper_binary)
    invoke(upper_binary, '--native', 'off', expected='3\n')

    # Do not publish an archive over a source, even with an unusual source suffix.
    collision_dir = root / 'collision'
    collision_source = put(collision_dir / 'modules/collision/1.gbin', 'println(71);')
    collision = put(collision_dir / 'module.gmod', json.dumps(dict(
        formatVersion=1, name='collision', version='1', main='modules/collision/1.gbin')))
    saved_source = collision_source.read_bytes()
    build(collision, fail=True)
    assert collision_source.read_bytes() == saved_source
    hardlink = project(root / 'hardlink', 'hardlink', 'println(72);')
    hardlink_output = artifact(hardlink)
    hardlink_output.parent.mkdir(parents=True)
    try:
        os.link(hardlink.parent / 'main.goat', hardlink_output)
    except OSError as error:
        print('Hardlink fixture unavailable:', error)
    else:
        saved_source = (hardlink.parent / 'main.goat').read_bytes()
        build(hardlink, fail=True)
        assert (hardlink.parent / 'main.goat').read_bytes() == saved_source
        assert hardlink_output.read_bytes() == saved_source

    # Refuse unsafe final destinations instead of deleting/replacing unrelated data.
    blocked = project(root / 'blocked', 'blocked', 'println("not executed");')
    blocked_output = artifact(blocked)
    blocked_output.mkdir(parents=True)
    sentinel = put(blocked_output / 'keep.txt', 'keep')
    build(blocked, fail=True)
    assert sentinel.read_text(encoding='utf-8') == 'keep'
    linked_output = project(root / 'linked-output', 'linked-output', 'println(1);')
    destination = artifact(linked_output)
    destination.parent.mkdir(parents=True)
    kept = put(root / 'kept.bin', 'must not overwrite')
    try:
        destination.symlink_to(kept)
    except OSError as error:
        if os.name != 'nt':
            raise
        print('Windows output symlink fixture unavailable:', error)
    else:
        build(linked_output, fail=True)
        assert kept.read_text(encoding='utf-8') == 'must not overwrite' and destination.is_symlink()

    # Existing source execution and legacy split compilation stay supported.
    standalone = put(root / 'standalone.goat', 'println(42);')
    invoke('--native', 'off', standalone, expected='42\n')
    invoke('--compile', '--native', 'off', standalone, expected='')
    legacy = standalone.with_suffix('.gbin')
    assert legacy.is_file()
    invoke('--run', '--native', 'off', legacy, expected='42\n')

    # Raw bytecode remains distinct from both GOATPG01 envelopes and ZIP modules.
    envelope = legacy.read_bytes()
    assert envelope[:8] == b'GOATPG01'
    code_size = struct.unpack_from('<Q', envelope, 24)[0]
    raw_code = envelope[96:96 + code_size]
    assert len(raw_code) == code_size and raw_code[:8] == b'GOATBC01'
    raw = root / 'raw-bytecode.gbin'
    for mode in ((), ('run',), ('--run',)):
        raw.write_bytes(raw_code)
        invoke(*mode, raw, '--native', 'off', '--lang', 'en', expected='42\n')
        assert raw.read_bytes() == raw_code
    bad_version = bytearray(raw_code)
    struct.pack_into('<I', bad_version, 8, 0xffffffff)
    invalid_raw = (b'', raw_code[:7], raw_code[:8], raw_code[:-1],
                   b'GOATBIN1' + raw_code[8:], bytes(bad_version))
    for data in invalid_raw:
        raw.write_bytes(data)
        for mode in ((), ('run',), ('--run',)):
            result = invoke(*mode, raw, '--native', 'off', '--lang', 'en',
                            fail=True, expected='')
            assert b'Invalid, incompatible or unreadable Goat binary' in result.stderr, result.stderr
            assert raw.read_bytes() == data

print(f'Module CLI checks: {count}/{count}')
if unsupported_replacements:
    print(f'Local ACL limitation: {unsupported_replacements} replacements verified only as fresh writes; CI remains strict')
