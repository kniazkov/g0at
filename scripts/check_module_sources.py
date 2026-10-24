#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Source snapshot, literal include and canonical-path identity regression fixtures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

EXE = str(Path(sys.argv[1]).resolve())
COUNT = 0


def run(path, descriptor=True, status=0, cwd=None, limits=()):
    global COUNT
    COUNT += 1
    p = subprocess.run([EXE, 'gmod' if descriptor else 'source', str(path), *map(str, limits)],
                       capture_output=True, timeout=40, cwd=cwd,
                       env=dict(os.environ, GOAT_MODULE_PATH='/must/not/be/searched'))
    assert p.returncode == status, (COUNT, p.returncode, status, p.stdout, p.stderr)
    result = json.loads(p.stdout)
    assert result['status'] == status, result
    return result


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8') if isinstance(text, str) else text)
    return path


def gmod(path, main='src/main.goat', **extra):
    return put(path, json.dumps(dict(formatVersion=1, name='sample', version='1.0.0',
                                    main=main, **extra), ensure_ascii=False))


def shape(result):
    return [(f['name'], f['external'], f['targets']) for f in result['files']]


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-sources-') as temp:
    root = Path(temp)
    project = root / 'project'
    other = root / 'elsewhere'
    other.mkdir()
    desc = gmod(project / 'app.gmod')
    main = put(project / 'src/main.goat', 'println("main");')
    put(project / 'src/unused.goat', 'include "missing";')
    assert shape(run(desc)) == [('src/main.goat', False, [])]
    # Normalized descriptor defaults and informational fields don't add sources.
    result = run(gmod(desc, dependencies={'unused': '9.0.0'}, native={}, description='', authors=[]))
    assert result['descriptor']['native'] == {'mode': 'auto', 'target': 'host'}
    assert 'description' not in result['descriptor'] and 'authors' not in result['descriptor']
    assert len(result['files']) == 1
    gmod(desc)
    put(main, 'include "vector.goat"; if(false){include "matrix.goat";}')
    put(project / 'src/vector.goat', 'include "common.goat";')
    put(project / 'src/matrix.goat', 'func(){include "common.goat";}')
    put(project / 'src/common.goat', 'include "main.goat";')
    expected = [('src/main.goat', False, [1, 3]), ('src/vector.goat', False, [2]),
                ('src/common.goat', False, [0]), ('src/matrix.goat', False, [2])]
    assert shape(run(desc)) == expected
    assert shape(run(desc, cwd=other)) == expected
    put(main, 'include "./vector.goat"; include "../src/vector.goat";')
    assert len(run(desc)['files']) == 3
    # Outside project includes, equal basename and equal bytes stay separate.
    put(root / 'shared/a/math.goat', '')
    put(root / 'shared/b/math.goat', '')
    put(main, 'include "../../shared/a/math.goat"; include "../../shared/b/math.goat";')
    assert shape(run(desc)) == [('src/main.goat', False, [1, 2]),
                                ('math.goat', True, []), ('math.goat', True, [])]
    put(root / 'project-other/outer.goat', '')
    put(main, 'include "../../project-other/outer.goat";')
    assert run(desc)['files'][1]['external']
    # Distinct builds own distinct snapshots (asserted in C harness).
    second = gmod(other / 'second.gmod', '../shared/a/math.goat')
    assert shape(run(second)) == [('math.goat', True, [])]
    assert shape(run(gmod(desc, str(root / 'shared/a/math.goat')))) == [('math.goat', True, [])]
    # Literal main, extensions, spaces and no environment/tilde substitution.
    for path in ['src/start', 'src/start.txt', '$HOME/main.goat', '~/main.goat',
                 '${HOME}/main.goat', '%USERPROFILE%/main.goat', 'юникод/😀.goat']:
        put(project / path, '')
        assert shape(run(gmod(desc, path))) == [(path, False, [])]
    if os.name != 'nt':
        for path in [' main.goat ', '   ', 'src\\literal.goat']:
            put(project / path, '')
            assert shape(run(gmod(desc, path))) == [(path, False, [])]
    else:
        put(project / 'src/backslash.goat', '')
        assert run(gmod(desc, 'src\\backslash.goat'))['files'][0]['name'] == 'src/backslash.goat'
        for path in ['C:main.goat', 'C:', '/src/main.goat', '\\src\\main.goat']:
            run(gmod(desc, path), status=2)
    put(project / 'missing.goat', '')
    run(gmod(desc, 'missing'), status=2)
    for path in ['', '\0x', 'a\0b', 'a\0']:
        run(gmod(desc, path), status=3)
    # Descriptor validation before any source I/O/defaults/overrides.
    valid = dict(formatVersion=1, name='sample', version='1.0.0', main='missing')
    for key in valid:
        invalid = dict(valid)
        del invalid[key]
        run(put(desc, json.dumps(invalid)), status=3)
    for extra in [dict(native={'mode': 'atuo'}), dict(native={'target': 42}),
                  dict(native={'unknown': True}), dict(dependencies={'bad name': '1.0.0'}),
                  dict(dependencies={'valid': 'bad version'}), dict(native=None)]:
        run(gmod(desc, 'missing', **extra), status=3)
    run(put(desc, '{"formatVersion":1,"formatVersion":1}'), status=3)
    run(put(desc, b'\xff'), status=3)
    # Source location errors, strings/comments decoys and literal decoding.
    gmod(desc)
    put(project / 'src/a.goat', '')
    put(main, '// include "bad"\n"include \\\"bad\\\""; /* include "bad" */\ninclude "a.goat";')
    assert len(run(desc)['files']) == 2
    put(main, 'var obj; obj.include;')
    assert len(run(desc)['files']) == 1
    for expr in ['include name;', 'include ("a.goat");', 'include "a" + ".goat";',
                 'include "";', 'include "a\\0.goat";', 'include;', 'include 3;',
                 'include "unterminated', 'if(false){include "a.goat";']:
        put(main, '\n' + expr)
        result = run(desc, status=4)
        assert result['row'] >= 2 and result['column'] > 0, result
    put(main, '\n  include "missing.goat";')
    result = run(desc, status=2)
    assert result['row'] == 2 and result['column'] == 3, result
    put(main, b'\xff')
    run(desc, status=4)
    put(main, b'\x00')
    run(desc, status=4)
    put(main, '')
    run(gmod(desc, 'src'), status=2)
    if hasattr(os, 'mkfifo'):
        fifo = project / 'pipe'
        os.mkfifo(fifo)
        run(gmod(desc, 'pipe'), status=2)
        put(main, 'include "../pipe";')
        run(gmod(desc), status=2)
    if os.name != 'nt':
        run(gmod(desc, '/dev/null'), status=2)
    # Symlink bases: both descriptor and source aliases resolve before relative paths.
    real = root / 'real'
    real_main = put(real / 'src/main.goat', 'include "helper.goat";')
    put(real / 'src/helper.goat', 'println("real");')
    real_desc = gmod(real / 'app.gmod')
    alias = root / 'alias'
    alias.mkdir()
    put(alias / 'src/helper.goat', 'include "wrong";')
    try:
        (alias / 'app.gmod').symlink_to(real_desc)
        (alias / 'run.goat').symlink_to(real_main)
    except OSError as error:
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1314:
            raise
        print('SKIP symlink fixtures: Windows privilege not held')
    else:
        assert shape(run(alias / 'app.gmod', cwd=other)) == shape(run(real_desc))
        assert shape(run(alias / 'run.goat', False, cwd=other)) == shape(run(real_main, False))
        assert shape(run(alias / 'run.goat', False)) == [('main.goat', False, [1]), ('helper.goat', False, [])]
        (real / 'src/again.goat').symlink_to(real / 'src/helper.goat')
        put(real_main, 'include "helper.goat"; include "again.goat";')
        assert shape(run(real_desc))[0][2] == [1, 1]
        (real / 'src/external.goat').symlink_to(root / 'shared/a/math.goat')
        put(real_main, 'include "external.goat";')
        assert shape(run(real_desc))[1] == ('math.goat', True, [])
    # Hard links retain distinct names AND containing directories for include bases.
    a = put(project / 'a/math.goat', 'include "helper.goat";')
    b = project / 'b/math.goat'
    b.parent.mkdir()
    os.link(a, b)
    put(a.parent / 'helper.goat', 'println("a");')
    put(b.parent / 'helper.goat', 'println("b");')
    put(main, 'include "../a/math.goat"; include "../b/math.goat";')
    gmod(desc)
    assert shape(run(desc)) == [('src/main.goat', False, [1, 3]), ('a/math.goat', False, [2]),
                                ('a/helper.goat', False, []), ('b/math.goat', False, [4]),
                                ('b/helper.goat', False, [])]
    run(desc, limits=(2,), status=5)
    run(desc, limits=(100, 8), status=2)
    run(desc, limits=(100, 1000, 5), status=2)
    # Explicit DFS stack: a deep cyclic chain never recurses in C.
    deep = root / 'deep'
    count = 1800
    for i in range(count):
        put(deep / f'{i}.goat', f'include "{(i + 1) % count}.goat";')
    result = run(deep / '0.goat', False)
    assert len(result['files']) == count
    assert result['files'][-1]['targets'] == [0]
    # Source graph is snapshot data only: no println/write/exit execution.
    put(main, 'exit(4); println("MUST NOT RUN"); if(false){include "a.goat";}')
    assert len(run(desc)['files']) == 2
print(f'{COUNT} source graph fixtures passed')
