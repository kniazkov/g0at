"""Exact-byte and semantic normalization tests against the production C API."""
import copy
import itertools
import json
import random
import subprocess
import sys
from pathlib import Path

EXE = str(Path(sys.argv[1]).resolve())
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docs/module-format-v1'))
import reference as ref

COUNT = 0
BASE = dict(formatVersion=1, bytecodeVersion=1, name='m', version='1', mainHash='0'*64,
            main=0, files=[dict(name='main.goat', entry=0, external=False)])


def run(v, mode='canonical-module', bad=False):
    global COUNT
    data = v if isinstance(v, bytes) else json.dumps(v, ensure_ascii=False).encode()
    result = subprocess.run([EXE, '--' + mode], input=data, capture_output=True, timeout=10)
    COUNT += 1
    if bad:
        assert result.returncode == 1, (data, result.returncode, result.stderr)
        return
    assert result.returncode == 0, (data, result.returncode, result.stderr)
    return result.stdout


def canonical(v, mode='canonical-module'):
    actual = run(v, mode)
    expected = ref.canonical(ref.normalize(ref.parse(v) if isinstance(v, bytes) else v,
                                         'gmod' if mode == 'canonical-gmod' else 'module'))
    assert actual == expected, (actual, expected)
    assert run(actual, mode) == actual, ('not idempotent', actual)
    return actual


def module_with(summaries, arity=2):
    f = dict(functionId=1, file=0, instruction=0,
             parameters=[dict(name=f'p{i}') for i in range(arity)],
             analysis=dict(formatVersion=1, summaries=summaries))
    return dict(BASE, functions=[f])


def fact(v):
    return module_with([dict(when={}, result=v)])


def main():
    for raw, expected in [
        (b' {"z": -0, "a": [true,false,null]} ', b'{"a":[true,false,null],"z":0}'),
        (b'{"z":"a\\n\\u0000/\\\"\\\\","a":0}', b'{"a":0,"z":"a\\u000a\\u0000/\\\"\\\\"}'),
        (b'"\\uD83D\\uDE00"', '"😀"'.encode()),
        (b'{"\\u0062":1,"a":2}', b'{"a":2,"b":1}'),
        (b'9007199254740993', b'9007199254740993'),
        (b'-9223372036854775808', b'-9223372036854775808'),
        (b'9223372036854775807', b'9223372036854775807')]:
        assert run(raw, 'canonical') == expected
    controls = ''.join(chr(i) for i in range(32))
    assert run(controls, 'canonical') == ('"'+''.join('\\u%04x'%i for i in range(32))+'"').encode()
    # Decoded key order, not escaped spelling or UTF-16 ordering; embedded NUL is significant.
    v = {'\U00010000':1, '\ue000':2, 'a\0z':3, 'a':4, 'é':5, 'e\u0301':6}
    assert run(v, 'canonical') == ref.canonical(v)
    assert run('é', 'canonical') != run('e\u0301', 'canonical')
    assert run([1, 2], 'canonical') != run([2, 1], 'canonical')
    for raw in [b'1.0', b'1e0', b'9223372036854775808', b'-9223372036854775809',
                b'NaN', b'{"x":1,"\\u0078":2}', b'{"a\\u0000":1,"a\\u0000":2}',
                b'"\xc0\xaf"', b'"\\ud800"', b'\xef\xbb\xbf{}', b'{} {}']:
        run(raw, 'canonical', True)
    for value in [-(1<<63), (1<<63)-1, 9007199254740992, 9007199254740993]:
        canonical(fact(dict(type='integer', value=value)))
        canonical(dict(BASE, externals={'n':dict(mutable=False, type='integer', value=value)}))
    assert canonical(fact(dict(type='integer', value=9007199254740992))) != canonical(
        fact(dict(type='integer', value=9007199254740993)))
    for value in [-(1<<63)-1, 1<<63, 1.0, True, '1']:
        run(fact(dict(type='integer', value=value)), bad=True)
    patterns = ['3ff8000000000000', '0000000000000001', '0000000000000000',
                '8000000000000000', '7ff0000000000000', 'fff0000000000000',
                '7ff8000000000001', '7ff8000000000002', 'fff0000000000001']
    outputs = [canonical(fact(dict(type='real', value=v))) for v in patterns]
    assert len(set(outputs)) == len(patterns)
    for value in ['', '3FF8000000000000', '0x00000000000000', '000000000000000 ', '0'*15, '0'*17, 1.5]:
        run(fact(dict(type='real', value=value)), bad=True)
        run(dict(BASE, externals={'x':dict(mutable=False, type='real', value=value)}), bad=True)
    for v in ['Я', '😀', '\0', ' ', 'é']:
        canonical(fact(dict(type='character', value=v)))
    for v in ['', 'ab', 'e\u0301', 65, None]:
        run(fact(dict(type='character', value=v)), bad=True)
    for kind, values in [('string', ['', ' ', ' a ', 'a\0b', 'é', 'e\u0301']),
                         ('boolean', [True, False]), ('null', [None])]:
        for v in values:
            canonical(fact(dict(type=kind, value=v)))
            canonical(dict(BASE, externals={'x':dict(mutable=False, type=kind, value=v)}))
    for f in [dict(type='string', value=False), dict(type='boolean', value=0),
              dict(type='null', value=''), dict(value=1), dict(type='array', value=[]),
              dict(type='function', value=1)]:
        run(fact(f), bad=True)
    assert canonical(fact(dict(type='string'))) != canonical(fact(dict(type='string', value='')))
    assert canonical(fact(dict(type='boolean'))) != canonical(fact(dict(type='boolean', value=False)))
    assert canonical(fact({})) != canonical(fact(dict(type='null')))
    reductions = [
        (dict(type='integer', range=dict(min=42, max=42)), dict(type='integer', value=42)),
        (dict(type='integer', value=42, range=dict(min=0, max=99)), dict(type='integer', value=42)),
        (dict(type='integer', range=dict(min=-(1<<63), max=(1<<63)-1)), dict(type='integer')),
        (dict(type='null', value=None), dict(type='null')),
        (dict(type='array', elements={}), dict(type='array')),
        (dict(type='array', empty=True, elements=dict(type='integer')), dict(type='array', empty=True)),
        (dict(type='array', elements=dict(type='array', elements={})),
         dict(type='array', elements=dict(type='array')))]
    for kind in ['boolean','integer','real','numeric','character','string','function','array','object']:
        reductions.append((dict(type=kind, nonNull=True), dict(type=kind)))
    for a, b in reductions:
        assert canonical(fact(a)) == canonical(fact(b))
        run(module_with([dict(when={}, result=a), dict(when={}, result=b)]), bad=True)
    canonical(fact(dict(nonNull=True)))
    for f in [dict(type='null', nonNull=True), dict(type='integer', nonNull=False),
              dict(type='integer', range=dict(min=2, max=1)),
              dict(type='integer', value=3, range=dict(min=0, max=2)),
              dict(type='integer', range=dict(min=-(1<<63)-1, max=0)),
              dict(type='real', range=dict(min=0, max=1)),
              dict(elements={}), dict(type='integer', elements={}), dict(type='array', elements=None),
              dict(type='array', elements=[]), dict(type='array', empty=False), dict(empty=True),
              dict(type='array', empty=True, elements=dict(type='integer', range=dict(min=2, max=1)))]:
        run(fact(f), bad=True)
    no_analysis = module_with([])
    for s in [dict(when={}), dict(when={}, result={}),
              dict(when={}, effects=dict(may=[], unknown=True)),
              dict(when=dict(arguments=[{},{}]), result={}, effects=dict(may=[], unknown=True)),
              dict(when=dict(arguments=[dict(type='integer'),{}]))]:
        assert canonical(module_with([s])) == canonical(no_analysis)
    run(module_with([dict(when={}), dict(when={}, result={})]), bad=True)
    run(module_with([dict(when={}), dict(when={}, result={})]), mode='module', bad=True)
    run(module_with([dict(when={}), dict(when=dict(arguments=[{},{}]))]), bad=True)
    run(module_with([dict(when={}, result={}, normalReturn=False)]), bad=True)
    for args in [[], [{}], [{},{},{}]]:
        run(module_with([dict(when=dict(arguments=args))]), bad=True)
    canonical(module_with([dict(when=dict(arguments=[]), normalReturn=False)], 0))
    for args in [[{},dict(type='integer'),{}], [dict(type='integer'),{},{}]]:
        v = module_with([dict(when=dict(arguments=args), result=dict(type='integer'))], 3)
        output = json.loads(canonical(v))
        assert output['functions'][0]['analysis']['summaries'][0]['when']['arguments'] == args
    pure = dict(when={}, effects=dict(may=[], unknown=False))
    assert canonical(module_with([pure])) != canonical(no_analysis)
    canonical(module_with([dict(when={}, normalReturn=False)]))
    effect_output = set()
    for order in itertools.permutations(['externalRead','externalWrite','input','output']):
        effect_output.add(canonical(module_with([dict(when={}, effects=dict(may=list(order), unknown=False))])))
    assert len(effect_output) == 1
    for effects in [{}, dict(may=[]), dict(unknown=True), dict(may=None,unknown=False),
                    dict(may=['input','input'], unknown=False), dict(may=['other'],unknown=True)]:
        run(module_with([dict(when={},effects=effects)]), bad=True)
    run(module_with([dict(when={},effects=dict(may=['input','output'],unknown=False)),
                     dict(when={},effects=dict(may=['output','input'],unknown=False))]), bad=True)
    f = module_with([])['functions'][0]
    for analysis in [{}, None, dict(formatVersion=2,summaries=[]), dict(formatVersion=1,summaries=[],extra=1)]:
        run(dict(BASE,functions=[dict(f, analysis=analysis)]), bad=True)
    summaries = [pure, dict(when={},result=dict(type='integer'))]
    assert canonical(module_with(summaries)) == canonical(module_with(summaries[::-1]))
    funcs = [dict(f,functionId=i) for i in [1,2,10]]
    output = set(canonical(dict(BASE,functions=list(order))) for order in itertools.permutations(funcs))
    assert len(output) == 1
    assert [f['functionId'] for f in json.loads(next(iter(output)))['functions']] == [1,2,10]
    run(dict(BASE,functions=[f,f]),bad=True)
    for external in [dict(mutable=True,type='integer'), dict(mutable=False,functionId=99),
                     dict(mutable=False,type='string',functionId=1),
                     dict(mutable=False,functionId=1,functionRef=dict(module='x',version='1',functionId=1))]:
        run(dict(BASE,functions=[f],externals={'x':external}),bad=True)
    canonical(dict(BASE,externals={'x':dict(mutable=False)}))
    canonical(dict(BASE,externals={'x':dict(mutable=True)}))
    canonical(dict(BASE,externals={'x':dict(mutable=False,functionRef=dict(module='x',version='1',functionId=1))}))
    # Projection has exactly the nine agreed fields, including analysis and dependency hashes.
    enriched = module_with([pure])
    enriched.update(description='description',authors=[dict(name='Ivan')],
                    dependencies=[dict(name='dep',version='1',hash='a'*64)],
                    externals={'x':dict(mutable=False)},
                    producers=[dict(name='gcc',version='1',buildId='abc')])
    normalized = ref.normalize(enriched)
    expected = ref.canonical({k:normalized[k] for k in ref.HASH_FIELDS if k in normalized})
    assert run(enriched,'main-metadata') == expected
    assert run(dict(enriched,description='changed',mainHash='1'*64),'main-metadata') == expected
    for filename in ['empty','math-1','math-2','two-versions']:
        v = ref.parse((ROOT/f'docs/module-format-v1/vectors/{filename}.module.json').read_bytes())
        canonical(v)
        if filename in ['empty','two-versions']:
            path = ROOT/f'docs/module-format-v1/vectors/{filename}.main.json'
            if path.exists():
                assert run(v,'main-metadata') == path.read_bytes().rstrip(b'\n')
    g = dict(formatVersion=1,name='m',version='1',main=' a.goat ')
    canonical(g,'canonical-gmod')
    assert canonical(dict(g,authors=[],description=''),'canonical-gmod') == canonical(g,'canonical-gmod')
    # Informational and unordered records normalize, ordered authors/files/parameters do not.
    producer = dict(name='compiler',version='v',buildId='id')
    canonical(dict(BASE,producers=[dict(producer,buildId='z'),producer]))
    run(dict(BASE,producers=[producer,producer]),bad=True)
    a, b = dict(name='A'), dict(name='B')
    assert canonical(dict(BASE,authors=[a,b])) != canonical(dict(BASE,authors=[b,a]))
    native = ref.parse((ROOT/'docs/module-format-v1/vectors/native-description.json').read_bytes())
    variant = dict(library='native/x.so',hash='f'*64,goatAbiVersion=1,
                   target=native['target'],specializations=native['specializations'])
    v = dict(BASE,functions=[f],native=[variant])
    canonical(v)
    # Equal target/ABI/hash keys use library only to make serialization deterministic.
    alias = copy.deepcopy(variant)
    alias['library'] = 'native/alias.so'
    assert canonical(dict(v,native=[variant,alias])) == canonical(dict(v,native=[alias,variant]))
    variant['target']['minimumOs'] += [0,0]
    variant['target']['cpuFeatures'] = ['z','sse2','a']
    canonical(v)
    # Seeded permutations of object keys have byte-for-byte identical output.
    rng = random.Random(180)
    for _ in range(20):
        pairs = list(enriched.items()); rng.shuffle(pairs)
        assert run(dict(pairs),'main-metadata') == expected
    print(f'Canonical JSON checks passed: {COUNT}')


if __name__ == '__main__':
    main()
