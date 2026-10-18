#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Compare native str, chained concatenation and quadratic roots with VM execution."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    exe = Path(sys.argv[1]).resolve()
    work = Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)

    def run(*args, env=None):
        result = subprocess.run([str(exe), '--lang', 'en', *map(str, args)],
                                capture_output=True, timeout=90, env=env)
        assert result.returncode == 0, (args, result.stderr)
        assert not result.stderr, result.stderr
        return result.stdout.replace(b'\r\n', b'\n')

    def check(source, calls, expected=None):
        reference = run('--optimize', 'none', '--native', 'off', source)
        if expected is not None:
            assert reference == expected, reference
        report = work / 'native.report'
        assert run('--native', 'required', '--save-native', report, source) == reference
        fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
        assert fields['preparation'] == 'ready' and fields['mode'] == 'required', fields
        assert fields['attempts'] == str(calls) and fields['succeeded'] == str(calls), fields
        assert fields['retries'] == '0', fields
        assert run('--compile', '--native', 'required', source) == b''
        source.unlink()
        env = os.environ.copy()
        env['CC'] = str(work / 'missing-compiler')
        assert run('--run', '--native', 'required', '--save-native', report,
                   source.with_suffix('.gbin'), env=env) == reference
        fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
        assert fields['succeeded'] == str(calls) and fields['retries'] == '0', fields

    source = work / 'quadratic.goat'
    shutil.copyfile(ROOT / 'example/native_quadratic.goat', source)
    generated = run('--print-c', source).decode('utf-8')
    assert 'goat_native_str_f64(g_context,' in generated
    assert generated.count('goat_native_string_concat(g_context,') >= 4
    assert 'GOAT_NATIVE_PURE' in generated
    check(source, 3, 'Нет корней\nОдин корень: 1.0\nДва корня: 1.0, 2.0\n'.encode())

    source = work / 'conversions.goat'
    source.write_text('''const number=func(n){return "["+n+", "+str(n)+"]";};
const literals=func(){return "кириллица 😀"+"\\\"\\\\";};
const values=func(n){return "bool="+(n>0)+", char="+chr(n)+", explicit="+str(n>0)+str(chr(n));};
const order=func(n){var x=n;return ""+(x=x+1)+","+(x=x+1)+","+str(x,x=x+1)+","+x;};
const alias=str;
const convert=func(n){return alias(n);};
println(number(0));
println(number(-9223372036854775807));
println(number(9223372036854775807));
println(number(1.0));
println(number(-0.0));
println(number(0.00000000001));
println(number(100000000000.0));
println(number(sqrt(-1.0)));
println(number(exp(1000.0)));
println(literals());
println(values(0));
println(values(128512));
println(order(1));
println(convert(3.5));
const same=func(s){return str(s);};
println(same(""));
println(same("hello"+chr(0)+"world"));
''', encoding='utf-8')
    assert b'2,3,3,4\n' in run('--optimize', 'none', '--native', 'off', source)
    check(source, 16)

    source = work / 'roots.goat'
    body = (ROOT / 'example/native_quadratic.goat').read_text(encoding='utf-8').split('\nprintln')[0]
    source.write_text(body + '''
println(quadratic(1.0, 0.0, -2.0));
println(quadratic(-1.0, 3.0, -2.0));
println(quadratic(0.5, 0.0, -2.0));
for(var i=0;i<100;i=i+1){
    var result=quadratic(1.0,-3.0,2.0);
    if(result!="Два корня: 1.0, 2.0") throw "bad result";
}
''', encoding='utf-8')
    check(source, 103)
    print('Native str, chained concatenation and quadratic roots OK')


if __name__ == '__main__':
    main()
