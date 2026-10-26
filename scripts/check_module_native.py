"""Independent native metadata selection fixtures; no fallback can hide a mismatch."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

EXE = str(Path(sys.argv[1]).resolve())
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
BYTECODE = (VECTORS / 'empty.bytecode.bin').read_bytes()
DESCRIPTOR = json.loads((VECTORS / 'native-description.json').read_text())
FEATURES = ('i686 sse2 sse3 ssse3 sse4_1 sse4_2 avx avx2 fma bmi1 bmi2 avx512f '
            'fp neon aes sha1 sha2 crc32 lse sve sve2').split()
OK, ARGUMENT, INVALID, NO_MATCH, AMBIGUOUS = range(5)
COUNT = 0


def target(os_name='linux', architecture='x86_64', family=None, features=None,
           os_version=None, runtime_version=None):
    return dict(os=os_name, architecture=architecture,
                bits=32 if architecture == 'x86' else 64, endian='little',
                minimumOs=os_version if os_version is not None else ([10] if os_name == 'windows' else [5, 4]),
                cpuFeatures=features if features is not None else {
                    'x86': ['i686'], 'x86_64': ['sse2'], 'aarch64': ['fp', 'neon']}[architecture],
                runtime=dict(family=family or ('msvcrt' if os_name == 'windows' else 'glibc'),
                             minimumVersion=runtime_version if runtime_version is not None else [0]))


def variant(profile=None, library='native/test.so', payload=b'unloaded-test-bytes', abi=1, **extra):
    return dict(library=library, hash=hashlib.sha256(payload).hexdigest(), goatAbiVersion=abi,
                target=copy.deepcopy(profile if profile is not None else target()),
                specializations=copy.deepcopy(DESCRIPTOR['specializations']), **extra)


def archive(variants, payloads=None, metadata_edit=None):
    metadata = dict(BASE, functions=[dict(functionId=1, file=0, instruction=0,
                                        parameters=[dict(name='a'), dict(name='b')])])
    metadata['mainHash'] = ref.main_hash(metadata, BYTECODE)
    metadata['native'] = variants
    if metadata_edit:
        metadata_edit(metadata)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        # Deliberately unrelated ZIP, input-array and normalized selection order.
        for v in reversed(variants):
            content = (payloads or {}).get(v['library'], b'unloaded-test-bytes')
            z.writestr(v['library'], content)
        z.writestr('bytecode.bin', BYTECODE)
        z.writestr('module.json', json.dumps(metadata, ensure_ascii=False).encode())
    return stream.getvalue()


def run(variants, profile=None, abis=(1,), mode='select', path=None,
        payloads=None, metadata_edit=None, env=None):
    global COUNT
    p = profile if profile is not None else target()
    raw = p if isinstance(p, bytes) else json.dumps(p, ensure_ascii=False).encode()
    framed = (struct.pack('<I', len(raw)) + raw + struct.pack('<I', len(abis))
              + b''.join(struct.pack('<I', a) for a in abis)
              + archive(variants, payloads, metadata_edit))
    process = subprocess.run([EXE, mode] + ([str(path)] if path else []), input=framed,
                             capture_output=True, timeout=30, env=env)
    COUNT += 1
    assert process.returncode == 0, (process.returncode, process.stderr, process.stdout, p)
    return json.loads(process.stdout)


def check(variants, status=OK, **kwargs):
    result = run(variants, **kwargs)
    assert result.get('status') == status, (result, status, variants, kwargs)
    if status != OK:
        assert result['selected'] is None, result
    return result


def profile_error(profile):
    result = run([variant()], profile=profile)
    assert result.get('profileError', 0), result


subprocess.run([EXE], check=True)
base = variant()
assert check([base])['library'] == base['library']
check([], NO_MATCH)
check([base], NO_MATCH, abis=())
check([base], NO_MATCH, abis=(2,))
check([base], abis=(3, 1, 2))
check([base], abis=(1, 1))
for abi in (2, 3, 4294967295):
    check([variant(abi=abi)], NO_MATCH)
    check([variant(abi=abi)], abis=(abi,))
# This API does not equate ABI versions to release versions or compare by order.
for supported in ((2,), (3,), (4294967295,)):
    check([base], NO_MATCH, abis=supported)

# Inspector explanations are the selector's own decisions, with independent reasons.
def reasons(v=base, **kwargs):
    return run([v], **kwargs)['compatibility'][0]

assert reasons()['reasons'] == 0
assert reasons(abis=())['reasons'] == 1 << 11
assert reasons(mode='unknown-os')['reasons'] == 1 << 0
assert reasons(mode='unknown-arch')['reasons'] == 1 << 1
assert reasons(mode='big-endian')['reasons'] == 1 << 3
assert reasons(mode='unknown-runtime')['reasons'] == 1 << 4
assert reasons(profile=target(os_version=[4]))['reasons'] == 1 << 5
assert reasons(variant(target(runtime_version=[2, 28])),
               profile=target(runtime_version=[2, 9]))['reasons'] == 1 << 6
assert reasons(mode='unknown-os-version')['reasons'] == 1 << 7
assert reasons(variant(target(runtime_version=[2, 28])),
               mode='unknown-runtime-version')['reasons'] == 1 << 8
assert reasons(mode='unknown-runtime-version')['reasons'] == 0
assert reasons(mode='no-features') == dict(status=NO_MATCH, reasons=1 << 9, missingCpu=1 << 1)
assert reasons(variant(target(features=['sse2', 'future-cpu'])))['reasons'] == 1 << 10
missing = reasons(variant(target(features=['avx2'])))
assert missing['reasons'] == 1 << 9 and missing['missingCpu'] == (1 << 6) | (1 << 7)
combined = reasons(variant(target('windows', 'x86', runtime_version=[2]),
                           library='native/other.dll', abi=2), profile=target(os_version=[1]))
assert combined['reasons'] == sum(1 << bit for bit in (0, 1, 2, 4, 5, 6, 9, 11))

# All supported architectural and runtime profiles, independent of machine running tests.
profiles = [target(), target(architecture='aarch64'), target(family='musl'),
            target('windows', family='msvcrt'), target('windows', family='ucrt'),
            target('windows', 'x86', 'msvcrt')]
for i, profile in enumerate(profiles):
    suffix = '.dll' if profile['os'] == 'windows' else '.so'
    v = variant(profile, library='native/profile' + suffix)
    for j, environment in enumerate(profiles):
        check([v], OK if i == j else NO_MATCH, profile=environment)
# 32-bit process does not select 64-bit DLL despite same Windows OS.
check([variant(profiles[3], library='native/64.dll')], NO_MATCH, profile=profiles[5])

# Numeric tuple comparisons, zero extension and uint32 extremes.
for field in ('minimumOs', 'runtime'):
    for need, available, compatible in [([2, 28], [2, 9], False), ([2, 9], [2, 28], True),
                                         ([5, 4], [5, 4], True), ([5, 4], [5, 4, 0, 0], True),
                                         ([5, 4, 1], [5, 4], False), ([5, 4], [5, 5], True),
                                         ([0], [0], True), ([4294967295], [4294967294], False),
                                         ([1, 4294967295], [2], True)]:
        requirement, environment = target(), target()
        if field == 'runtime':
            requirement[field]['minimumVersion'] = need
            environment[field]['minimumVersion'] = available
        else:
            requirement[field], environment[field] = need, available
        check([variant(requirement)], OK if compatible else NO_MATCH, profile=environment)
for available, compatible in [([10, 0, 19044], False), ([10, 0, 19045], True),
                              ([10, 0, 22621], True), ([11], True)]:
    v = variant(target('windows', os_version=[10, 0, 19045]), library='native/win.dll')
    check([v], OK if compatible else NO_MATCH,
          profile=target('windows', os_version=available))
# Unknown knowledge cannot masquerade as a discovered [0] version.
for mode in ('unknown-os', 'unknown-runtime', 'unknown-os-version', 'unknown-arch',
             'big-endian', 'no-features'):
    check([base], NO_MATCH, mode=mode)
check([variant(target(runtime_version=[2, 28]))], NO_MATCH, mode='unknown-runtime-version')
# A producer-established [0] floor is explicitly unrestricted, not guessed.
check([base], mode='unknown-runtime-version')

# Baselines are inherent requirements, even if producer omits them.
for profile in profiles:
    sparse = copy.deepcopy(profile)
    sparse['cpuFeatures'] = []
    check([variant(profile, library='native/x.dll' if profile['os'] == 'windows' else 'native/x.so')],
          NO_MATCH, profile=sparse)
    omitted = variant(sparse, library='native/x.dll' if profile['os'] == 'windows' else 'native/x.so')
    check([omitted], profile=profile)
    check([omitted], NO_MATCH, profile=sparse)
# Unknown feature names remain structurally valid, never satisfiable, including if profile claims one.
for feature in ('future-cpu', 'SSE2', 'sse2\0ignored', '🚀'):
    requirement = target(features=['sse2', feature])
    check([variant(requirement)], NO_MATCH)
    check([variant(requirement)], NO_MATCH, profile=requirement)
# Architectural impossibilities are structural, not an excuse to ignore a foreign target.
for profile, feature in [(target(), 'neon'), (target(architecture='aarch64'), 'avx2')]:
    profile['cpuFeatures'].append(feature)
    check([variant(profile)], INVALID)
# Filename suffix must agree with the stated OS, even for unsuitable variants.
check([variant(library='native/linux.dll')], INVALID)
check([variant(profiles[3], library='native/windows.so')], INVALID)
# A malformed late foreign variant takes precedence over an early ambiguity.
invalid_foreign = variant(target(architecture='aarch64', features=['avx']),
                          library='native/z-foreign.so')
for variants in ([variant(library='native/a.so'), variant(library='native/b.so'), invalid_foreign],
                 [invalid_foreign, variant(library='native/a.so'), variant(library='native/b.so')]):
    check(variants, INVALID)
profile_error(target(architecture='aarch64', features=['avx']))
# AVX-family features require usable AVX (the host detector additionally checks XCR0).
for feature in ('avx2', 'fma', 'avx512f'):
    requirement = target(features=['sse2', feature])
    check([variant(requirement)], NO_MATCH, profile=target(features=['sse2', feature]))
    complete = target(features=['sse2', 'avx', feature])
    check([variant(requirement)], profile=complete)
# SSE successor requirements imply their predecessor chain on synthetic profiles.
for feature, predecessors in [('sse3', ['sse2']), ('ssse3', ['sse2', 'sse3']),
                              ('sse4_1', ['sse2', 'sse3', 'ssse3']),
                              ('sse4_2', ['sse2', 'sse3', 'ssse3', 'sse4_1'])]:
    check([variant(target(features=[feature]))],
          profile=target(features=[*predecessors, feature]))
    for missing in predecessors:
        check([variant(target(features=[feature]))], NO_MATCH,
              profile=target(features=[f for f in [*predecessors, feature] if f != missing]))
check([variant(target(architecture='aarch64', features=['sve2']))], NO_MATCH,
      profile=target(architecture='aarch64', features=['fp', 'neon', 'sve2']))
check([variant(target(architecture='aarch64', features=['sve2']))],
      profile=target(architecture='aarch64', features=['fp', 'neon', 'sve', 'sve2']))

# No first/best/most-specific preference; duplicate suitable variants are ambiguous.
a = variant(library='native/a.so')
b = variant(library='native/b.so')
for variants in ([a, b], [b, a]):
    check(variants, AMBIGUOUS)
    check(variants, NO_MATCH, profile=profiles[1])
newer = variant(target(runtime_version=[2, 28]), library='native/new.so')
check([a, newer], AMBIGUOUS, profile=target(runtime_version=[2, 39]))
check([a, newer], profile=target(runtime_version=[2, 9]))
foreign = variant(profiles[1], library='native/foreign.so')
for variants in ([foreign, a], [a, foreign]):
    assert check(variants)['library'] == 'native/a.so'

# All library bytes and hashes validate before filtering, including unknown ABI/CPU/runtime.
for incompatible in [foreign, variant(abi=2, library='native/future.so'),
                     variant(target(family='musl'), library='native/musl.so'),
                     variant(target(features=['sse2', 'future']), library='native/future.so')]:
    for corrupted in (a, incompatible):
        result = run([a, incompatible], payloads={corrupted['library']: b'tampered'})
        assert result.get('containerError') == 13, result
        wrong = copy.deepcopy([a, incompatible])
        next(v for v in wrong if v['library'] == corrupted['library'])['hash'] = '0' * 64
        assert run(wrong).get('containerError') == 13

# cCompiler is preserved information, not compatibility or main-hash input.
expected_hash = check([base])['mainHash']
for compiler in ({'name': 'GCC', 'version': '16.1'}, {'name': 'Clang', 'version': '20'},
                 {'name': ' GCC ', 'version': ' A\u0000B '}):
    result = check([variant(cCompiler=compiler)])
    assert result['mainHash'] == expected_hash
for compiler in (None, {}, {'name': 'GCC'}, {'name': '', 'version': '1'},
                 {'name': 'GCC', 'version': ''}, {'name': 1, 'version': '1'},
                 {'name': 'GCC', 'version': '1', 'extra': True}):
    assert run([variant(cCompiler=compiler)]).get('containerError') == 11

# Strict profiles and mandatory metadata reject null, wrong types, closed-object extras.
for edit in (lambda p: p.update(unknown=True), lambda p: p.update(bits=32),
             lambda p: p.update(endian='big'), lambda p: p.update(minimumOs=[]),
             lambda p: p.update(minimumOs=[1, 2, 3, 4, 5]),
             lambda p: p.update(minimumOs=[-1]), lambda p: p.update(minimumOs=[4294967296]),
             lambda p: p.update(minimumOs=[1.0]), lambda p: p.update(cpuFeatures=['sse2', 'sse2']),
             lambda p: p['runtime'].update(family='ucrt'),
             lambda p: p['runtime'].update(minimumVersion=None)):
    p = target(); edit(p); profile_error(p)
for field in target():
    p = target(); del p[field]; profile_error(p)
profile_error(b'{"os":"linux","os":"linux"}')
profile_error(target('windows', os_version=[6, 1]))

# Host facts are about the running process and do not follow environment changes.
host = json.loads(subprocess.check_output([EXE, 'host'], timeout=20))
for fake in ('MINGW32', 'MINGW64', 'UCRT64', 'not-a-runtime'):
    environment = dict(os.environ, MSYSTEM=fake, PATH='nonexistent')
    changed = json.loads(subprocess.check_output([EXE, 'host'], env=environment, timeout=20))
    assert changed == host, (changed, host)
assert host['little']
assert host['bits'] == {1: 32, 2: 64, 3: 64}[host['architecture']]
assert host['os'] in (1, 2) and host['runtime'] in (1, 2, 3, 4)
assert host['osVersion']['known'], host
if host['runtime'] == 1:
    assert host['runtimeVersion']['known'], host
names = [name for index, name in enumerate(FEATURES) if host['features'] & (1 << index)]
architecture = {1: 'x86', 2: 'x86_64', 3: 'aarch64'}[host['architecture']]
family = {1: 'glibc', 2: 'musl', 3: 'msvcrt', 4: 'ucrt'}[host['runtime']]
os_name = {1: 'linux', 2: 'windows'}[host['os']]
base_features = {'x86': ['i686'], 'x86_64': ['sse2'], 'aarch64': ['fp', 'neon']}[architecture]
assert all(f in names for f in base_features), host
actual = target(os_name, architecture, family, names,
                host['osVersion']['components'], host['runtimeVersion']['components'])
requirement = target(os_name, architecture, family, base_features,
                     host['osVersion']['components'], [0])
suffix = '.dll' if os.name == 'nt' else '.so'
check([variant(requirement, library='native/host' + suffix)], mode='detect')
# A baseline shared library really runs on every CI platform, not a simulated match.
# Requirements discovery for production modules is #211; this fixture's pure C
# arithmetic has no runtime calls, and deliberately contains no Goat ABI adapter.
with tempfile.TemporaryDirectory(prefix='goat-native-metadata-') as directory:
    directory = Path(directory)
    source = directory / 'fixture.c'
    library = directory / ('fixture' + suffix)
    source.write_text('''#ifdef _WIN32
__declspec(dllexport)
#endif
int goat_metadata_fixture(int value) { return value * 2 + 4; }
''')
    compiler = os.environ.get('CC') or ('gcc' if os.name == 'nt' else 'cc')
    baseline = {'x86': '-march=i686', 'x86_64': '-march=x86-64', 'aarch64': '-march=armv8-a'}[architecture]
    flags = [baseline, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
    macros = subprocess.check_output([compiler, *flags, '-include', 'stdlib.h', '-dM', '-E', '-x', 'c', str(source)], timeout=30).decode()
    if os_name == 'windows':
        # The CI compiler is the executable's toolchain, independent of MSYSTEM.
        expected_runtime = 4 if '#define _UCRT ' in macros else 3
        assert host['runtime'] == expected_runtime, (host, expected_runtime)
    elif '#define __GLIBC__ ' in macros:
        assert host['runtime'] == 1, host
    if architecture == 'x86_64':
        assert '#define __SSE2__ 1' in macros
        for feature in ('__AVX__', '__AVX2__', '__AVX512F__'):
            assert '#define ' + feature + ' ' not in macros
    elif architecture == 'aarch64':
        assert '#define __ARM_NEON 1' in macros
        for feature in ('__ARM_FEATURE_SVE', '__ARM_FEATURE_CRYPTO', '__ARM_FEATURE_ATOMICS'):
            assert '#define ' + feature + ' ' not in macros
    subprocess.run([compiler, *flags, '-shared', *([] if os.name == 'nt' else ['-fPIC']),
                    str(source), '-o', str(library)], check=True, timeout=60)
    payload = library.read_bytes()
    v = variant(requirement, library='native/fixture' + suffix, payload=payload)
    result = check([v], mode='load', path=library, payloads={v['library']: payload})
    assert result['nativeResult'] == 42
print(f'Native metadata checks passed: {COUNT}; real baseline native call on {os_name}/{architecture}/{family}')
