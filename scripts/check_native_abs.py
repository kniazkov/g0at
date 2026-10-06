#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compare VM/native abs, binding identity, argument effects and numeric edge cases."""
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
        'numeric': ('const f=func(n){return abs(n);};println(f(-7));println(f(-3.5));'
                    'println(f(-9223372036854775808));println(f(0));', '7\n3.5\n9223372036854775807\n0\n'),
        'loop': ('const f=func(n){var r=0;for(var i=-n+1;i<n;i++)r=r+abs(i);return r;};'
                 'println(f(100));', '9900\n'),
        'alias': ('const magnitude=abs;const f=func(n){return magnitude(n);};println(f(-8));', '8\n'),
        'effects': ('const f=func(n){var r=abs(n++,n++);return r*100+n;};println(f(-3));', '199\n'),
        'shadow': ('const abs=func(n){return n+10;};const f=func(n){return abs(n);};println(f(-3));', '7\n'),
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
        print('[ ok ] native abs', name)

    # Rebinding and lexical shadowing must retain VM semantics.
    for name, source, expected in [
        ('replace', 'const f=func(n){return abs(n);};println(f(-3));abs=func(n){return 99;};println(f(-3));', '3\n99\n'),
        ('nested_shadow', 'const f=func(n){return abs(n);};const change=func(){abs=func(n){return 88;};};change();println(f(-3));', '3\n'),
        ('invalid', 'const f=func(n){return abs(n);};try {println(f("bad"));} catch(e) {println("caught");}', 'caught\n'),
        ('missing', 'const f=func(){return abs();};try {println(f());} catch(e) {println("caught");}', 'caught\n'),
    ]:
        path = output / (name + '.goat')
        path.write_text(source, encoding='utf-8')
        for mode in ['off', 'auto']:
            result = run('--native', mode, path)
            assert result.returncode == 0 and result.stdout == expected, result
        print('[ ok ] abs rejection', name)

    # Invoke the generated adapters directly with values absent from source observations.
    path = output / 'edges.goat'
    path.write_text('const f=func(n){return abs(n);};f(1);f(1.0);', encoding='utf-8')
    result = run('--save-c', path)
    assert result.returncode == 0, result
    host = output / 'host.c'
    host.write_text('''#include "edges.c"
#include <assert.h>
int main(void) {
    const goat_native_module_v1_t *m = goat_native_query_v1(1);
    assert(m && m->entry_count == 2);
    for (unsigned i=0;i<m->entry_count;i++) {
        const goat_native_entry_v1_t *e=&m->entries[i];
        goat_native_value_v1_t a={0},r={0}; a.type=e->parameter_types[0];
        if(a.type==GOAT_NATIVE_I64) {
            int64_t values[]={INT64_MIN,INT64_MAX,-1,0,1};
            for(unsigned j=0;j<5;j++) {
                a.value.integer=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK);
                int64_t expected=values[j]==INT64_MIN?INT64_MAX:(values[j]<0?-values[j]:values[j]);
                assert(r.type==GOAT_NATIVE_I64 && r.value.integer==expected);
            }
        } else {
            double values[]={-0.0,0.0,-INFINITY,INFINITY,NAN,-3.5};
            for(unsigned j=0;j<6;j++) {
                a.value.real=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                if(isnan(values[j])) assert(isnan(r.value.real));
                else assert(r.value.real==fabs(values[j]) && !signbit(r.value.real));
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
    print('[ ok ] abs adapters: INT64_MIN, signed zero, NaN and infinities')


if __name__ == '__main__':
    main()
