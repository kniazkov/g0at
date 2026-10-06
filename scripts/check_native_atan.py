#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compare VM/native atan and atan2, binding identity, argument effects and numeric edge cases."""
import os
from pathlib import Path
import subprocess
import sys


def main():
    goat, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GOAT_LANGUAGE='en')

    def run(*args):
        return subprocess.run([str(goat), *map(str, args)], env=env,
                              capture_output=True, text=True, timeout=30)

    cases = {
        'numeric': ('const f=func(n){return atan(n);};const g=func(a,b){return atan(a,b);};'
                    'println(f(1));println(f(-1));println(f(0.5));println(g(1,1));println(g(-1,1));'
                    'println(g(1,-1));println(g(2,1));',
                    '0.7853981634\n-0.7853981634\n0.463647609\n0.7853981634\n-0.7853981634\n'
                    '2.35619449019\n1.10714871779\n'),
        'effects': ('const f=func(n){var x=n;var r=atan(x=x+1,x=x+2);return r*100+x;};'
                    'const g=func(n){var x=n;var r=atan(x=x+1,x=x+2,x=x+3);return r*100+x;};'
                    'println(f(1));println(g(1));',
                    '96.72952180016\n93.21700546672\n'),
        'alias': ('const arctan=atan;const f=func(n){return arctan(n);};println(f(1));',
                  '0.7853981634\n'),
        'shadow': ('const atan=func(n){return n+10;};const f=func(n){return atan(n);};'
                   'println(f(-3));', '7\n'),
    }
    for name, (source, expected) in cases.items():
        path = output / (name + '.goat')
        path.write_text(source, encoding='utf-8')
        for mode in ['off', 'required']:
            report = output / (name + '-' + mode + '.report')
            result = run('--native', mode, '--save-native', report, path)
            assert result.returncode == 0 and result.stdout == expected, result
            if mode == 'required':
                fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
                assert int(fields['succeeded']) > 0 and fields['attempts'] == fields['succeeded'], fields
                assert fields['retries'] == '0', fields
        print('[ ok ] native atan', name)

    # Rebinding and lexical shadowing must retain VM semantics.
    for name, source, expected in [
        ('replace', 'const f=func(n){return atan(n);};println(f(-3));'
                    'atan=func(n){return 99;};println(f(-3));', '-1.2490457724\n99\n'),
        ('nested_shadow', 'const f=func(n){return atan(n);};const change=func(){'
                          'atan=func(n){return 88;};};change();println(f(-3));',
         '-1.2490457724\n'),
        ('invalid', 'const f=func(n){return atan(n);};try {println(f("bad"));} '
                    'catch(e) {println("caught");}', 'caught\n'),
        ('missing', 'const f=func(){return atan();};try {println(f());} '
                    'catch(e) {println("caught");}', 'caught\n'),
    ]:
        path = output / (name + '.goat')
        path.write_text(source, encoding='utf-8')
        for mode in ['off', 'auto']:
            result = run('--native', mode, path)
            assert result.returncode == 0 and result.stdout == expected, result
        print('[ ok ] atan rejection', name)

    # Invoke the generated adapters directly with values absent from source observations.
    path = output / 'edges.goat'
    path.write_text('const f=func(n){return atan(n);};const g=func(a,b){return atan(a,b);};'
                    'f(1);g(1,1);f(1.0);', encoding='utf-8')
    result = run('--save-c', path)
    assert result.returncode == 0, result
    host = output / 'host.c'
    host.write_text('''#include "edges.c"
#include <assert.h>
#include <math.h>
int main(void) {
    const goat_native_module_v1_t *m = goat_native_query_v1(1);
    assert(m && m->entry_count == 3);
    for (unsigned i=0;i<m->entry_count;i++) {
        const goat_native_entry_v1_t *e=&m->entries[i];
        goat_native_value_v1_t a[2]={{0},{0}}, r={0};
        a[0].type=e->parameter_types[0];
        if (e->parameter_count == 1) {
            if (a[0].type==GOAT_NATIVE_I64) {
                int64_t values[]={INT64_MIN,INT64_MAX,-2,-1,0,1,2};
                for (unsigned j=0;j<7;j++) {
                    a[0].value.integer=values[j];
                    assert(e->invoke(1,1,a,&r)==GOAT_NATIVE_OK);
                    assert(r.type==GOAT_NATIVE_F64 && r.value.real==atan((double)values[j]));
                }
            } else {
                assert(a[0].type==GOAT_NATIVE_F64);
                double values[]={-0.0,0.0,-INFINITY,INFINITY,NAN,-3.5};
                for (unsigned j=0;j<6;j++) {
                    a[0].value.real=values[j];
                    assert(e->invoke(1,1,a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                    if (isnan(values[j])) assert(isnan(r.value.real));
                    else assert(r.value.real==atan(values[j]));
                }
            }
        } else {
            assert(e->parameter_count == 2);
            a[1].type=e->parameter_types[1];
            if (a[0].type==GOAT_NATIVE_I64) {
                assert(a[1].type==GOAT_NATIVE_I64);
                int64_t yv[]={INT64_MIN,-2,-1,0,1,2}, xv[]={-2,-1,1,2,INT64_MAX,3};
                for (unsigned j=0;j<6;j++) {
                    a[0].value.integer=yv[j]; a[1].value.integer=xv[j];
                    assert(e->invoke(1,2,a,&r)==GOAT_NATIVE_OK);
                    assert(r.type==GOAT_NATIVE_F64
                           && r.value.real==atan2((double)yv[j],(double)xv[j]));
                }
            } else {
                assert(a[0].type==GOAT_NATIVE_F64 && a[1].type==GOAT_NATIVE_F64);
                double yv[]={-0.0,0.0,-1.0,1.0,NAN}, xv[]={-1.0,1.0,0.0,-0.0,1.0};
                for (unsigned j=0;j<5;j++) {
                    a[0].value.real=yv[j]; a[1].value.real=xv[j];
                    assert(e->invoke(1,2,a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                    if (isnan(yv[j]) || isnan(xv[j])) assert(isnan(r.value.real));
                    else assert(r.value.real==atan2(yv[j],xv[j]));
                }
            }
        }
    }
    return 0;
}
''', encoding='utf-8')
    exe = output / 'host.exe'
    subprocess.run([env.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    str(host), '-lm', '-o', str(exe)], check=True, capture_output=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=30)
    print('[ ok ] atan adapters: INT64 boundaries, signed zero, NaN and infinities')


if __name__ == '__main__':
    main()
