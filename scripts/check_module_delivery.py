#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Public producer-to-consumer acceptance, including compiler-free native delivery.

Runs unchanged in Linux x86-64/AArch64 and Windows MINGW32/MINGW64/UCRT64.
Native execution is measured, never inferred from a successful VM-compatible result.
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

from inspect_report_test_support import schema_check
from module_execution_support import put

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

GOAT, INSPECT = [str(Path(arg).resolve()) for arg in sys.argv[1:3]]
MERGE = str(Path(INSPECT).with_name('goat-merge.exe' if os.name == 'nt' else 'goat-merge'))
CC = shutil.which(sys.argv[3]) or sys.argv[3]
SCHEMA_PATH = ROOT / 'docs/module-format-v1/inspect-report.schema.json'
SCHEMA = json.loads(SCHEMA_PATH.read_text(encoding='utf-8'))
COUNT = 0


def invoke(executable, *args, env=None, fail=False, expected=None):
    global COUNT
    result = subprocess.run([executable, *map(str, args)], capture_output=True,
                            env=env, timeout=180, stdin=subprocess.DEVNULL)
    assert (result.returncode != 0) == fail, (args, result.returncode, result.stdout, result.stderr)
    if expected is not None:
        assert result.stdout.decode('utf-8').replace('\r\n', '\n') == expected, (
            args, result.stdout, result.stderr)
    if fail:
        assert result.stderr or result.stdout, ('failure without diagnostic', args)
    COUNT += 1
    return result


def contents(path, deflated=False):
    with zipfile.ZipFile(path) as archive:
        if deflated:
            assert all(part.compress_type == zipfile.ZIP_DEFLATED for part in archive.infolist())
        files = {name: archive.read(name) for name in archive.namelist()}
    doc = json.loads(files['module.json'])
    assert doc['mainHash'] == ref.main_hash(doc, files['bytecode.bin'])
    for variant in doc.get('native', []):
        assert hashlib.sha256(files[variant['library']]).hexdigest() == variant['hash']
    return doc, files


with tempfile.TemporaryDirectory(prefix='goat-delivery-') as temporary:
    root = Path(temporary)
    source = root / 'source'
    modules = root / 'staging'
    distribution = root / 'delivered'
    distribution.mkdir()
    build_env = dict(os.environ, GOAT_MODULE_PATH=str(modules))

    def producer_environment(compiler):
        env = dict(build_env)
        if os.name == 'nt':
            # cc1/collect2 live below GCC's libexec directory. Their DLL lookup
            # must find this compiler's runtime before another MSYS2 ABI's bin.
            env['PATH'] = str(Path(compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
        return env

    def build(name, version, code, mode='required', dependencies=None, output=None,
              compiler=CC, native_target='host'):
        directory = source / (name + '-' + version + '-' + mode)
        descriptor = dict(formatVersion=1, name=name, version=version, main='main.goat')
        if dependencies:
            descriptor['dependencies'] = dependencies
        put(directory / 'main.goat', code)
        path = put(directory / 'module.gmod', json.dumps(descriptor))
        destination = output or modules
        invoke(GOAT, 'build', path, '--module-dir', destination, '--native', mode,
               '--cc', compiler, '--native-target', native_target,
               env=producer_environment(compiler), expected='')
        archive = destination / name / (version + '.gbin')
        contents(archive, deflated=True)
        return archive

    leaf_code = ('external square=func(x){return x*x;}; '
                 'println("leaf 2 init"); square(3);')
    build('leaf', '2', leaf_code)
    build('leaf', '1', 'external square=func(x){return x+1000;}; square(3);')
    build('middle', 'release',
          'const leaf=import "leaf"; println("middle init"); '
          'external answer=leaf.square(7); external inc=func(x){return x+1;}; inc(1);',
          dependencies={'leaf': '2'})
    app_code = ('const middle=import "middle@release"; println("app init"); '
                'external twice=func(x){return x*2;}; println(twice(middle.answer)); '
                'println(9223372036854775807); println(-9223372036854775807);')
    app = build('delivery', 'opaque-release', app_code)
    off = build('delivery', 'opaque-release', app_code, mode='off', output=root / 'off')
    auto = build('delivery', 'opaque-release', app_code, mode='auto', output=root / 'auto')
    # Run/build mode affects only native parts, not the portable program identity.
    native_doc, native_files = contents(app)
    off_doc, off_files = contents(off)
    assert native_doc['mainHash'] == off_doc['mainHash']
    assert native_files['bytecode.bin'] == off_files['bytecode.bin']
    auto_doc, auto_files = contents(auto)
    assert auto_doc['mainHash'] == native_doc['mainHash'] and auto_doc['native']
    assert auto_files['bytecode.bin'] == native_files['bytecode.bin']
    assert not off_doc.get('native')
    # Shared producer goldens: the same source graph must serialize identically
    # in 32/64-bit Windows and x86-64/AArch64 Linux, not just match locally.
    assert native_doc['mainHash'] == '06ab143298bb8cd0a951a60efd33587c0ea89c17e438d3b2113043d0703b713b'
    assert hashlib.sha256(native_files['bytecode.bin']).hexdigest() == (
        'ba0a5dd1d608878e46e3d18c707bb2e3a1d21a53e6e62a93114485b051e83ccf')
    target = native_doc['native'][0]['target']
    print('Delivery target:', json.dumps(target, sort_keys=True), flush=True)
    # Read the executable, not Python's pointer width (CI Python may be 64-bit
    # while the Goat process is MINGW32). Native execution above/below tests it.
    image = Path(GOAT).read_bytes()
    if os.name == 'nt':
        pe = struct.unpack_from('<I', image, 0x3c)[0]
        assert image[pe:pe + 4] == b'PE\0\0'
        machine = struct.unpack_from('<H', image, pe + 4)[0]
        expected_arch, bits = {0x14c: ('x86', 32), 0x8664: ('x86_64', 64)}[machine]
    else:
        assert image[:4] == b'\x7fELF' and image[5] == 1
        machine = struct.unpack_from('<H', image, 18)[0]
        expected_arch, bits = {62: ('x86_64', 64), 183: ('aarch64', 64)}[machine]
        assert image[4] == 2
    assert target['architecture'] == expected_arch and target['bits'] == bits, target

    shutil.copytree(modules, distribution / 'modules')
    portable = distribution / 'app.gbin'
    shutil.copyfile(app, portable)
    plain = distribution / 'plain.gbin'
    shutil.copyfile(off, plain)
    # Also preserve a different valid identity for the dependency-conflict test.
    replacement = build('leaf', '2', leaf_code.replace('x*x', 'x+999'),
                        output=root / 'alternate')
    alternate_bytes = replacement.read_bytes()

    def merge(destination, inputs, variant_count):
        result = invoke(MERGE, destination, *inputs, env=build_env)
        text = result.stdout.decode('utf-8').replace('\r\n', '\n')
        suffix = f': "delivery"@"opaque-release"; {variant_count} native variants\n'
        assert text.startswith('"') and text.endswith(suffix), (text, result.stderr)
        assert not result.stderr, result.stderr
        contents(destination, deflated=True)

    merged = distribution / 'merged-roundtrip.gbin'
    merge(merged, [app, off], 1)
    mixed = foreign_only = None
    foreign_outputs = []
    if os.name == 'nt':
        # Real compiler outputs, not relabelled DLL bytes: both CRTs must be
        # available even when this suite runs in the 32-bit MINGW32 job.
        runtime_archives = {}
        for environment, runtime, native_target in (
                ('MINGW64', 'msvcrt', 'windows-x86_64-mingw64'),
                ('UCRT64', 'ucrt', 'windows-x86_64-ucrt64')):
            compiler_key = 'GOAT_DELIVERY_' + environment + '_CC'
            compiler = os.environ.get(compiler_key)
            assert compiler and Path(compiler).is_absolute() and Path(compiler).is_file(), (
                'missing mandatory absolute compiler path', compiler_key)
            # Diagnose real compiler/helper loading before an opaque build error.
            compiler_env = producer_environment(compiler)
            machine = invoke(compiler, '-dumpmachine', env=compiler_env).stdout.strip()
            assert machine == b'x86_64-w64-mingw32', (compiler, machine)
            macros = invoke(compiler, '-dM', '-E', '-x', 'c', '-', env=compiler_env).stdout
            assert b'#define __x86_64__ 1' in macros, (compiler, macros)
            directory = root / ('producer-' + environment)
            built = build('delivery', 'opaque-release', app_code, output=directory,
                          compiler=compiler, native_target=native_target)
            doc, _ = contents(built)
            assert doc['mainHash'] == native_doc['mainHash']
            assert doc['native'][0]['target']['runtime']['family'] == runtime
            assert doc['native'][0]['target']['architecture'] == 'x86_64'
            runtime_archives[runtime] = built
            foreign_outputs.append(directory)
        # Avoid rebuilding the same own target twice in the merge: debug paths
        # can give different DLL hashes even when portable identities are equal.
        foreign = [archive for runtime, archive in runtime_archives.items()
                   if bits == 32 or runtime != target['runtime']['family']]
        mixed = distribution / 'mixed-windows.gbin'
        foreign_only = distribution / 'foreign-windows.gbin'
        merge(mixed, [app, *foreign], 3 if bits == 32 else 2)
        merge(foreign_only, foreign, 2 if bits == 32 else 1)
        mixed_doc, _ = contents(mixed, deflated=True)
        assert {v['target']['runtime']['family'] for v in mixed_doc['native']} == {'msvcrt', 'ucrt'}
        assert len(mixed_doc['native']) == (3 if bits == 32 else 2)
    shutil.rmtree(source)
    shutil.rmtree(modules)
    shutil.rmtree(root / 'off')
    shutil.rmtree(root / 'auto')
    shutil.rmtree(root / 'alternate')
    for directory in foreign_outputs:
        shutil.rmtree(directory)
    assert not list(root.rglob('*.goat')) and not list(root.rglob('*.gmod'))
    empty_path = root / 'no-tools'
    empty_path.mkdir()
    consumer = dict(os.environ, PATH=str(empty_path), CC=str(empty_path / 'no-cc'),
                    CXX=str(empty_path / 'no-cxx'), GOAT_MODULE_PATH='',
                    MSYSTEM='deliberately-not-the-build-runtime')
    expected = 'leaf 2 init\nmiddle init\napp init\n98\n9223372036854775807\n-9223372036854775807\n'
    before = {p.relative_to(distribution): p.read_bytes() for p in distribution.rglob('*.gbin')}

    def execute(path, mode, expected_output=expected, fail=False):
        report = root / ('counters-' + str(COUNT) + '.txt')
        result = invoke(GOAT, 'run', path, '--native', mode, '--save-native', report,
                        '--cc', empty_path / 'no-cc', env=consumer,
                        fail=fail, expected=expected_output)
        if not fail:
            fields = dict(line.split('=', 1) for line in report.read_text(encoding='utf-8').splitlines())
            if mode == 'off':
                assert int(fields['succeeded']) == 0, fields
            else:
                assert int(fields['succeeded']) > 0 and int(fields['bound']) > 0, fields
        return result

    for mode in ('off', 'auto', 'required'):
        execute(portable, mode)
    execute(merged, 'required')
    execute(plain, 'off')
    # With no own native part, required rejects before initializer effects.
    execute(plain, 'required', expected_output='', fail=True)
    assert before == {p.relative_to(distribution): p.read_bytes() for p in distribution.rglob('*.gbin')}

    def inspect(path, fail=False, language='en', full=False):
        locale = {'en': 'en_US.UTF-8', 'ru': 'ru_RU.UTF-8'}[language]
        result = invoke(INSPECT, '--json', path,
                        env=dict(consumer, LANG=locale, LC_ALL=locale), fail=fail)
        assert not result.stderr, result.stderr
        report = json.loads(result.stdout)
        schema_check(report, SCHEMA, SCHEMA, SCHEMA_PATH.parent)
        assert report['reportVersion'] == 1 and report['success'] == (not fail)
        codes = {d['code'] for d in report['diagnostics'] if d['severity'] == 'error'}
        assert bool(codes) == fail, report
        return report if full else codes

    inspect(portable)

    def own_variants(report):
        own = [module for module in report['modules']
               if module['metadata'] and module['metadata']['name'] == 'delivery']
        assert len(own) == 1, report
        return own[0]['native']

    # Unsupported native ABI does not invalidate portable bytecode or trigger
    # a speculative load. The unselected embedded ABI is deliberately untouched.
    unknown_doc = copy.deepcopy(native_doc)
    unknown_doc['native'][0]['goatAbiVersion'] = 2
    unknown = distribution / 'unknown-abi.gbin'
    with zipfile.ZipFile(unknown, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in native_files.items():
            archive.writestr(name, ref.canonical(unknown_doc) if name == 'module.json' else data)
    assert contents(unknown)[0]['mainHash'] == native_doc['mainHash']
    execute(unknown, 'off')
    execute(unknown, 'auto')
    execute(unknown, 'required', expected_output='', fail=True)
    for variant in own_variants(inspect(unknown, full=True)):
        assert variant['compatibility']['state'] == 'incompatible', variant
        assert variant['loading']['state'] == 'not_checked', variant
        assert variant['loading']['reason']['code'] == 'NO_COMPATIBLE_VARIANT', variant

    if mixed is not None:
        for fake_environment in ('MINGW32', 'MINGW64', 'UCRT64'):
            consumer['MSYSTEM'] = fake_environment
            for mode in ('off', 'auto', 'required'):
                execute(mixed, mode)
        consumer['MSYSTEM'] = 'deliberately-not-the-build-runtime'
        variants = own_variants(inspect(mixed, full=True))
        compatible = [variant for variant in variants
                      if variant['compatibility']['state'] == 'compatible']
        assert len(compatible) == 1, variants
        assert compatible[0]['loading']['state'] == 'checked', compatible
        assert compatible[0]['loading']['probe']['state'] == 'passed', compatible
        assert all(variant['loading']['state'] == 'not_checked' for variant in variants
                   if variant not in compatible), variants
        mixed_doc, _ = contents(mixed)
        selected_library = compatible[0]['library']
        selected = next(v for v in mixed_doc['native'] if v['library'] == selected_library)
        assert selected['target']['architecture'] == target['architecture']
        assert selected['target']['runtime']['family'] == target['runtime']['family']
        execute(foreign_only, 'off')
        execute(foreign_only, 'auto')
        execute(foreign_only, 'required', expected_output='', fail=True)
        variants = own_variants(inspect(foreign_only, full=True))
        assert all(v['compatibility']['state'] == 'incompatible'
                   and v['loading']['state'] == 'not_checked' for v in variants), variants
    inspect(distribution / 'absent.gbin', fail=True)
    # Exact opaque version and recorded hash, never highest-version guessing.
    # Shared-identity diamond conflicts and all conflicting provenance paths are
    # covered by check_module_graph.py's real repository fixtures, not recreated here.
    dependency = distribution / 'modules/leaf/2.gbin'
    leaf_bytes = dependency.read_bytes()
    dependency.unlink()
    for mode in ('off', 'auto', 'required'):
        execute(portable, mode, expected_output='', fail=True)
    missing_codes = inspect(portable, fail=True)
    assert missing_codes == inspect(portable, fail=True, language='ru')
    dependency.write_bytes(alternate_bytes)
    for mode in ('off', 'auto', 'required'):
        execute(portable, mode, expected_output='', fail=True)
    inspect(portable, fail=True)
    dependency.write_bytes(leaf_bytes)
    execute(portable, 'required')

    # All entries are validated before any initializer, even in native-off mode.
    bad_cases = []
    broken_native = dict(native_files)
    library = native_doc['native'][0]['library']
    broken_native[library] += b'changed native payload'
    bad_cases.append(('native-hash', broken_native))
    broken_bytecode = dict(native_files)
    broken_bytecode['bytecode.bin'] += b'changed portable payload'
    bad_cases.append(('bytecode-hash', broken_bytecode))
    broken_json = dict(native_files)
    broken_json['module.json'] = b'{bad json'
    bad_cases.append(('json', broken_json))
    for name, files in bad_cases:
        path = distribution / (name + '.gbin')
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for part, data in files.items():
                archive.writestr(part, data)
        for mode in ('off', 'auto', 'required'):
            execute(path, mode, expected_output='', fail=True)
        assert inspect(path, fail=True) == inspect(path, fail=True, language='ru')

    # Stored and mixed archives preserve decompressed identity and native bytes.
    for method in ('stored', 'mixed'):
        path = distribution / (method + '.gbin')
        with zipfile.ZipFile(path, 'w') as archive:
            for index, (part, data) in enumerate(native_files.items()):
                compression = zipfile.ZIP_STORED if method == 'stored' or index % 2 else zipfile.ZIP_DEFLATED
                archive.writestr(part, data, compress_type=compression)
        doc, files = contents(path)
        assert doc['mainHash'] == native_doc['mainHash'] and files == native_files
        execute(path, 'required')
        inspect(path)

print(f'Module delivery end-to-end checks passed: {COUNT}')
