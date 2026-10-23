"""Exercise the production C descriptor reader, not the Python reference parser."""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

EXE = str(Path(sys.argv[1]).resolve())
ROOT = Path(__file__).resolve().parents[1]
COUNT = 0
G = dict(formatVersion=1, name='geometry', version='1.2.0', main='src/main.goat')
M = dict(formatVersion=1, bytecodeVersion=1, name='geometry', version='1.2.0',
         mainHash='0' * 64, main=0, files=[dict(name='main.goat', entry=0, external=False)])


def read(value, kind='module', error=None):
    global COUNT
    data = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False).encode()
    result = subprocess.run([EXE, '--' + kind], input=data, capture_output=True, timeout=10)
    COUNT += 1
    if error:
        assert result.returncode == 1, (data, result.returncode, result.stderr)
        if error is not True:
            assert error.encode() in result.stderr, (data, result.stderr, error)
        return None
    assert result.returncode == 0, (data, result.returncode, result.stderr)
    return json.loads(result.stdout)


def modify(value, path, replacement):
    result = copy.deepcopy(value)
    at = result
    for key in path[:-1]:
        at = at[key]
    at[path[-1]] = replacement
    return result


def objects(value, path=()):
    if isinstance(value, dict):
        yield path, value
        for key, item in value.items():
            yield from objects(item, path + (key,))
    elif isinstance(value, list):
        for i, item in enumerate(value):
            yield from objects(item, path + (i,))


def unknown_fields(value, kind):
    for path, obj in objects(value):
        # dependencies/externals are the only open maps, whose entries have schemas.
        if path in [('dependencies',), ('externals',)]:
            continue
        altered = dict(obj, surprise=1)
        read(modify(value, path, altered) if path else altered, kind, 'unknown JSON field')
        altered = dict(obj)
        # A recognized prefix followed by NUL must never compare as the real key.
        if altered:
            key = next(iter(altered))
            altered[key + '\0suffix'] = altered.pop(key)
            read(modify(value, path, altered) if path else altered, kind, 'unknown JSON field')


def main():
    expected = dict(G, dependencies={}, native=dict(mode='auto', target='host'))
    assert read(G, 'gmod') == expected
    assert read(dict(G, dependencies={}, native={}), 'gmod') == expected
    for native in [dict(mode='auto'), dict(target='host')]:
        assert read(dict(G, native=native), 'gmod') == expected
    for kind, base in [('gmod', G), ('module', M)]:
        assert read(base, kind)
        for key in base:
            omitted = dict(base)
            del omitted[key]
            read(omitted, kind, True)
            read(dict(base, **{key: None}), kind, True)
        unknown_fields(base, kind)
        for field in ['description', 'license', 'homepage', 'authors']:
            empty = [] if field == 'authors' else ''
            assert read(dict(base, **{field: empty}), kind) == read(base, kind)
            read(dict(base, **{field: None}), kind, True)
        for text in [' ', 'страница пока не создана', 'A\0B', 'e\u0301', '😀']:
            v = dict(base, description=text, license=text, homepage=text,
                     authors=[dict(name=text, email=text)])
            normalized = read(v, kind)
            for field in ['description', 'license', 'homepage', 'authors']:
                assert normalized[field] == v[field]
        assert read(dict(base, authors=[dict(name='Ivan', email='')]), kind) == read(
            dict(base, authors=[dict(name='Ivan')]), kind)
        for authors in [[dict(name='')], [dict(name=None)], [dict(email='x')],
                        [dict(name='Ivan', email=None)], [dict(name='Ivan', extra='x')],
                        [dict(name='Ivan'), dict(name='Ivan', email='')]]:
            read(dict(base, authors=authors), kind, True)
        for number in ['1.0', '1e0', '"1"', 'true', 'null', '9223372036854775808',
                       '-9223372036854775809', '18446744073709551616']:
            raw = json.dumps(base).replace('"formatVersion": 1', '"formatVersion": ' + number)
            read(raw.encode(), kind, 'invalid JSON field')
        read(dict(base, formatVersion=2), kind, 'unsupported format version')
        raw = json.dumps(base).encode()
        for invalid in [b'\xef\xbb\xbf' + raw, raw + b' trailing', raw + b' {}', b'/*x*/' + raw,
                        raw[:-1] + b',}', raw.replace(b'geometry', b'\xc0\xaf'),
                        raw.replace(b'geometry', b'\xed\xa0\x80'), raw.replace(b'geometry', b'\xf4\x90\x80\x80'),
                        raw.replace(b'geometry', b'\\ud800'), raw.replace(b'geometry', b'\\udc00')]:
            read(invalid, kind, 'invalid JSON or UTF-8')
        read(raw[:-1] + b',"\\u006eame":"other"}', kind, 'duplicate JSON key')
        read(raw[:-1] + b',"authors":[{"name":"x","\\u006eame":"y"}]}',
             kind, 'duplicate JSON key')
    for path in ['', '\0a', 'a\0b', 'a\0']:
        read(dict(G, main=path), 'gmod', True)
    for path in [' ', ' main.goat ', 'src/😀.goat']:
        assert read(dict(G, main=path), 'gmod')['main'] == path
    # Use the exact returned path, with no fallback to a trimmed name.
    if sys.platform != 'win32':
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'main.goat').write_text('wrong')
            (folder / ' main.goat ').write_text('right')
            path = read(dict(G, main=' main.goat '), 'gmod')['main']
            assert (folder / path).read_text() == 'right'
    for native in [None, [], dict(mode='atuo'), dict(mode='auto\0bad'), dict(target=7),
                   dict(target=''), dict(target='host\0bad'), dict(target='host+'),
                   dict(target='host++avx'), dict(extra=1)]:
        read(dict(G, native=native), 'gmod', True)
    for deps in [None, [], {'con.txt': '1'}, {'valid': 'BAD'}, {'unused': None}]:
        read(dict(G, dependencies=deps), 'gmod', True)
    assert read(dict(G, native=dict(mode='off', target='linux-x86_64+avx2'),
                     dependencies={'unused': '2.0'}), 'gmod')['dependencies'] == {'unused': '2.0'}
    for deps in [None, {}, '', [dict(name='math', version='1')],
                 [dict(name='math', version='1', hash='A' * 64)],
                 [dict(name='math', version='1', hash='0' * 63)],
                 [dict(name='math', version='1', hash='0' * 64, unknown=1)]]:
        read(dict(M, dependencies=deps), error=True)
    assert read(dict(M, dependencies=[])) == read(M)
    dep = dict(name='math', version='1', hash='0' * 64)
    assert len(read(dict(M, dependencies=[dep, dict(dep, version='2')]))['dependencies']) == 2
    for second in [dep, dict(dep, hash='1' * 64)]:
        read(dict(M, dependencies=[dep, second]), error=True)
    for value in [-1, 4294967296, 1.0, 1e20, '1', True, None]:
        read(modify(M, ('files', 0, 'entry'), value), error=True)
    for value in [0, 4294967295]:
        assert read(modify(M, ('files', 0, 'entry'), value))['files'][0]['entry'] == value
    for value in [1, 4294967295, 4294967296]:
        read(dict(M, main=value), error=True)
    raw = json.dumps(M).replace('"entry": 0', '"entry": -0').encode()
    assert read(raw)['files'][0]['entry'] == 0
    read(json.dumps(M).replace('"entry": 0', '"entry": 0e0').encode(), error=True)
    read(dict(M, bytecodeVersion=2), error='unsupported format version')
    read(dict(M, bytecodeVersion=1.0), error=True)
    # A complete nested descriptor covers every closed record in the v1 schema.
    native = json.loads((ROOT / 'docs/module-format-v1/vectors/native-description.json').read_text())
    rich = dict(M, dependencies=[dep], authors=[dict(name='Ivan', email='связаться через сайт')],
                producers=[dict(name='compiler', version='dev', buildId='build')],
                functions=[dict(functionId=1, file=0, instruction=0, parameters=[dict(name='x'), dict(name='y')],
                    analysis=dict(formatVersion=1, summaries=[dict(when=dict(arguments=[{}, {}]),
                        result=dict(type='array', elements=dict(type='integer', range=dict(min=-2, max=3))),
                        effects=dict(may=['input', 'output'], unknown=False))]))],
                externals=dict(f=dict(mutable=False, type='function', functionId=1),
                               g=dict(mutable=False, functionRef=dict(module='math', version='1', functionId=1))),
                native=[dict(library='native/example.so', hash='0' * 64, goatAbiVersion=1,
                             target=native['target'], specializations=native['specializations'],
                             cCompiler=dict(name='gcc', version='16'))])
    assert read(rich) == rich
    unknown_fields(rich, 'module')
    read(modify(rich, ('functions', 0, 'analysis', 'formatVersion'), 2), error='unsupported format version')
    for value in [-(1 << 63), (1 << 63)-1]:
        v = copy.deepcopy(rich)
        v['functions'][0]['analysis']['summaries'][0]['result'] = dict(type='integer', value=value)
        assert read(v) == v
    for value in [-(1 << 63)-1, 1 << 63, 1.5]:
        v = copy.deepcopy(rich)
        v['functions'][0]['analysis']['summaries'][0]['result'] = dict(type='integer', value=value)
        read(v, error=True)
    for path, value in [(('native', 0, 'target', 'bits'), 64.0),
                        (('native', 0, 'target', 'cpuFeatures'), ['avx', 'avx']),
                        (('native', 0, 'target', 'minimumOs'), []),
                        (('native', 0, 'target', 'minimumOs'), [1, 2, 3, 4, 5]),
                        (('native', 0, 'library'), 'native/../evil.so'),
                        (('native', 0, 'specializations', 0, 'symbol'), 'goat_fn_01_v1'),
                        (('functions', 0, 'file'), 1), (('functions', 0, 'functionId'), 0),
                        (('functions', 0, 'analysis', 'summaries', 0, 'effects', 'may'), ['input', 'input'])]:
        read(modify(rich, path, value), error=True)
    read(dict(rich, functions=rich['functions'] * 2), error=True)
    # Preflight also bounds nesting in unknown fields, before walking a schema.
    read(b'{"x":' + b'[' * 300 + b'0' + b']' * 300 + b'}', error='nesting limit')
    for filename in ['empty', 'math-1', 'math-2', 'two-versions']:
        read((ROOT / f'docs/module-format-v1/vectors/{filename}.module.json').read_bytes())
    # Every identity-bearing descriptor position uses the shared validator.
    for value in ['con.txt', 'lpt0.bin', 'a.', 'A', 'a/b', 'a\\b', 'a\0b', 'ёж', '', 'a' * 256]:
        read(dict(G, name=value), 'gmod', True)
        read(dict(G, version=value), 'gmod', True)
        read(dict(G, dependencies={value: '1'}), 'gmod', True)
        read(dict(G, dependencies={'unused': value}), 'gmod', True)
        read(dict(M, name=value), error=True)
        read(dict(M, version=value), error=True)
        read(dict(M, dependencies=[dict(dep, name=value)]), error=True)
        read(dict(M, dependencies=[dict(dep, version=value)]), error=True)
        read(modify(rich, ('externals', 'g', 'functionRef', 'module'), value), error=True)
        read(modify(rich, ('externals', 'g', 'functionRef', 'version'), value), error=True)
    for kind, base in [('gmod', G), ('module', M)]:
        normalized = read(dict(base, name='a' * 255, version='1' * 250), kind)
        assert normalized['name'] == 'a' * 255 and normalized['version'] == '1' * 250
        read(dict(base, version='1' * 251), kind, True)
    distinct = read(dict(M, dependencies=[dict(dep, version='1.02'), dict(dep, version='1.2')]))
    assert [d['version'] for d in distinct['dependencies']] == ['1.02', '1.2']
    print(f'Module JSON black-box checks passed: {COUNT}')


if __name__ == '__main__':
    main()
