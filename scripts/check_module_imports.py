#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Static direct imports, exact defaults, source sites and deterministic ordering."""
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

EXE = str(Path(sys.argv[1]).resolve())
COUNT = 0


def run(path, descriptor=True, status=0, limit=None, cwd=None):
    global COUNT
    COUNT += 1
    args = [EXE, 'gmod' if descriptor else 'source', str(path)]
    if limit is not None:
        args.append(str(limit))
    proc = subprocess.run(args, capture_output=True, timeout=40, cwd=cwd,
                          env=dict(os.environ, GOAT_MODULE_PATH='/must/not/be/searched'))
    assert proc.returncode == status, (COUNT, args, proc.returncode, status,
                                        proc.stdout, proc.stderr)
    result = json.loads(proc.stdout)
    assert result['status'] == status, result
    if status:
        assert result['message'], result
        assert not result.get('dependencies'), result
    return result


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8') if isinstance(text, str) else text)
    return path


def gmod(path, **extra):
    return put(path, json.dumps(dict(formatVersion=1, name='sample', version='1.0.0',
                                    main='src/main.goat', **extra), ensure_ascii=False))


def pairs(result, key='dependencies'):
    return [(dep['name'], dep['version']) for dep in result[key]]


def sites(result, name, version):
    return next(dep['sites'] for dep in result['dependencies']
                if (dep['name'], dep['version']) == (name, version))


subprocess.run([EXE], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='goat-imports-') as temp:
    root = Path(temp)
    project = root / 'project'
    desc = gmod(project / 'app.gmod')
    main = put(project / 'src/main.goat', '')
    other = root / 'elsewhere'
    other.mkdir()
    assert not pairs(run(desc))
    # Defaults alone cannot create dependencies; warning ordering is canonical.
    gmod(desc, dependencies={'zeta': '9', 'alpha': '1'})
    result = run(desc)
    assert not pairs(result)
    assert pairs(result, 'unused_defaults') == [('alpha', '1'), ('zeta', '9')]
    # All syntax, even code that is never invoked, contributes imports.
    code = ('exit(4); println("MUST NOT RUN");\n'
            'if(false){import "zeta";}\n'
            'func(){import "alpha@1";}\n'
            'import "alpha"; import "alpha@2"; import "alpha@1";')
    put(main, code)
    result = run(desc)
    assert pairs(result) == [('alpha', '1'), ('alpha', '2'), ('zeta', '9')]
    assert len(sites(result, 'alpha', '1')) == 3
    assert not pairs(result, 'unused_defaults')
    assert run(desc, cwd=other) == result
    # An explicit different version neither consumes a default nor rewrites it.
    put(main, 'import "alpha@2";')
    result = run(desc)
    assert pairs(result) == [('alpha', '2')]
    assert pairs(result, 'unused_defaults') == [('alpha', '1'), ('zeta', '9')]
    # No default from self identity, neighboring descriptor, explicit occurrence,
    # environment/search path or the fact that a name has only one known version.
    for text in ['import "sample";', 'import "missing";',
                 'import "absent@7"; import "absent";']:
        put(main, text)
        run(desc, status=5)
    put(main, 'import "alpha";')
    run(main, descriptor=False, status=5)
    put(main, 'import "alpha@1";')
    assert pairs(run(main, descriptor=False)) == [('alpha', '1')]
    # Strings/comments/member access do not reserve or trigger import parsing.
    put(main, '// import "missing"\n/* import variable; */\n'
              '"import \\"missing\\""; obj.import; obj /* comment */ . import;\n'
              'imported; imports; import "alpha@1";')
    assert pairs(run(desc)) == [('alpha', '1')]
    # Original Unicode scalar offsets, one-based row/column, not UTF-8 bytes.
    prefix = '// 😀é\n  '
    put(main, prefix + 'import "alpha@1";\nimport "alpha@1";')
    result = run(desc)
    assert sites(result, 'alpha', '1') == [
        dict(file=0, row=2, column=3, offset=len(prefix)),
        dict(file=0, row=3, column=1, offset=len(prefix + 'import "alpha@1";\n'))]
    # Supported escapes are decoded before identity validation, not kept as text.
    for escaped in [r'a\tb@1', r'a\nb@1', r'a\"b@1', r'a\\b@1']:
        put(main, 'import "' + escaped + '";')
        run(desc, status=4)
    # Dependency sorting is bytewise exact, not semantic version sorting.
    specs = ['zeta@1', 'alpha@2', 'alpha@10']
    for permutation in itertools.permutations(specs):
        put(main, ''.join(f'import "{spec}";' for spec in permutation))
        assert pairs(run(desc)) == [('alpha', '10'), ('alpha', '2'), ('zeta', '1')]
    put(main, 'import "alpha@1.02"; import "alpha@1.2"; import "alpha@latest";')
    assert pairs(run(desc)) == [('alpha', '1.02'), ('alpha', '1.2'), ('alpha', 'latest')]
    # Include graph is scanned once per canonical file, including external files,
    # cycles and dead include branches; unrelated disk files are never inspected.
    put(project / 'src/unused.goat', 'import "missing";')
    put(project / 'src/helper.goat', 'import "shared@1"; include "main.goat";')
    put(root / 'external.goat', 'import "external@3"; import "shared@1";')
    put(main, 'import "shared@1"; include "helper.goat"; include "./helper.goat";'
              'if(false){include "../../external.goat";}')
    result = run(desc)
    assert result['files'] == ['src/main.goat', 'src/helper.goat', 'external.goat']
    assert pairs(result) == [('external', '3'), ('shared', '1')]
    assert [site['file'] for site in sites(result, 'shared', '1')] == [0, 1, 2]
    assert len(run(main, descriptor=False)['files']) == 3
    # A failure in an included file keeps its file index and source location.
    put(project / 'src/helper.goat', '\n  import "missing";')
    result = run(desc, status=5)
    assert (result['file'], result['row'], result['column'], result['offset']) == (1, 2, 3, 3)
    # Exact import count, not deduplicated pair count, is bounded.
    put(main, 'import "alpha@1"; import "alpha@1";')
    assert len(sites(run(desc, limit=2), 'alpha', '1')) == 2
    result = run(desc, limit=1, status=6)
    assert result['offset'] == len('import "alpha@1"; ')
    assert len(sites(run(desc, limit=0), 'alpha', '1')) == 2
    # The default boundary must remain practical for repeated and distinct pairs.
    put(main, 'import "alpha@1";\n' * 65536)
    assert len(sites(run(desc), 'alpha', '1')) == 65536
    put(main, 'import "alpha@1";\n' * 65537)
    assert run(desc, status=6)['row'] == 65537
    put(main, ''.join(f'import "m{i}@1";\n' for i in range(16384)))
    assert len(run(desc)['dependencies']) == 16384
    # Invalid literal operands are syntax errors, not inferred dynamic imports.
    for expression in ['import variable;', 'import ("alpha@1");', 'import 3;',
                       'import;', 'import', 'import "";', 'import "a\\0b";',
                       'import "alpha" + "@1";', 'import "alpha" "@1";',
                       'import "alpha@1"();']:
        put(main, '\n  ' + expression)
        result = run(desc, status=3)
        assert result['row'] == 2 and result['column'] >= 3, result
    for spec in ['a@', '@1', 'a@1@2', 'Upper@1', 'a@Upper', 'a/b@1',
                 'a b@1', 'con@1', 'a@con', 'a.@1', 'é@1', '-a@1',
                 'a' * 256 + '@1', 'a@' + '1' * 251]:
        put(main, '\n  import ' + json.dumps(spec, ensure_ascii=False) + ';')
        result = run(desc, status=4)
        assert (result['file'], result['row'], result['column'], result['offset']) == (0, 2, 3, 3)
    # Source graph rejects malformed UTF-8, tokens/brackets before collection.
    for malformed in [b'\xff', b'\x00', 'import "unterminated', 'if(false){import "a@1";']:
        put(main, malformed)
        run(desc, status=104)
    # Invalid defaults fail full descriptor validation even if unused/overridden.
    put(main, 'import "alpha@2";')
    for defaults in [{'bad name': '1'}, {'alpha': 'bad version'}, {'alpha': 1}]:
        run(gmod(desc, dependencies=defaults), status=103)
print(f'{COUNT} static import fixtures passed')
