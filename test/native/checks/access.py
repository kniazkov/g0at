#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Access-aware native rejection, compiler fallback, retries and copied-function isolation."""
import os
from pathlib import Path
import subprocess
import sys


def main():
    goat, output = map(lambda p: Path(p).resolve(), sys.argv[1:3])
    env = dict(os.environ, GOAT_LANGUAGE='en')

    def run(source, *options, expected=None, environment=env, success=True):
        result = subprocess.run([str(goat), *map(str, options), str(source)],
                                env=environment, capture_output=True, text=True, timeout=120)
        assert (result.returncode == 0) == success, (result.args, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (result.args, result.stdout, expected)
        return result

    blocked = output / 'blocked.goat'
    blocked.write_text('var obj={private x=42;}; '
                       'const f=func(value){return value.x;}; '
                       'try f(obj); catch(e) println(e);', encoding='utf-8')
    for optimize in ('none', 'all'):
        run(blocked, '--optimize', optimize, '--native', 'off', expected='ACCESS_DENIED\n')
    denied = run(blocked, '--compile', '--native', 'required', success=False, expected='')
    assert 'Access checks require VM execution' in denied.stderr, denied.stderr
    assert not blocked.with_suffix('.gbin').exists()

    # The argument expressions execute once, even when the native depth guard retries in VM.
    source = output / 'retry.goat'
    source.write_text('public marker=0; '
                      'const f=func(n){if(n<=0)return 0;return f(n-1)+1;}; '
                      'var hits=199; var extras=0; '
                      'println(f(++hits,++extras)); println(f(3)); '
                      'println(hits); println(extras); '
                      'var obj={private secret=42;public read=func(){return secret;};}; '
                      'println(obj.read()); try obj.secret; catch(e) println(e); '
                      'println(obj);', encoding='utf-8')
    expected = '200\n3\n200\n1\n42\nACCESS_DENIED\n{"read":func}\n'
    for mode in ('off', 'auto', 'required'):
        report = output / (mode + '.report')
        run(source, '--native', mode, '--save-native', report, expected=expected)
        fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
        if mode != 'off':
            assert fields['attempts'] == '2' and fields['succeeded'] == '1', fields
            assert fields['retries'] == '1', fields
    run(source, '--compile', '--native', 'required', expected='')
    run(source.with_suffix('.gbin'), '--run', '--native', 'required', expected=expected)
    fallback = run(source, '--native', 'auto', expected=expected,
                   environment=dict(env, CC='goat-access-test-missing-compiler'))
    assert fallback.stderr, 'Compiler fallback must report its cause'
    print('Access rejection, saved native artifacts, compiler fallback and retry checks passed')


if __name__ == '__main__':
    main()
