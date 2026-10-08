#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Compare VM/native libm builtins (including abs and atan/atan2) and verify the generated
native adapters."""
import os
from pathlib import Path
import subprocess
import sys


UNARY = {
    'abs': [-9223372036854775808, -7, -3.5, 0, 5],
    'acos': [-1, -0.5, 0, 0.5, 1],
    'asin': [-1, -0.5, 0, 0.5, 1],
    'atan': [-10, -1, -0.5, 0, 0.5, 1, 10],
    'cbrt': [-27, -8, -1, 0, 1, 8, 27],
    'ceil': [-2.5, -0.5, 0, 0.5, 2.5],
    'cos': [-10, -1, -0.5, 0, 0.5, 1, 10],
    'cosh': [-5, -1, 0, 1, 5],
    'exp': [-5, -1, 0, 1, 5],
    'exp2': [-5, -1, 0, 1, 5],
    'expm1': [-5, -1, 0, 1, 5],
    'floor': [-2.5, -0.5, 0, 0.5, 2.5],
    'int': [-9223372036854775808, -7, -3.5, 0, 5],
    'log': [0.5, 1, 2, 10],
    'log10': [0.5, 1, 2, 10],
    'log1p': [-0.5, 0, 1, 10],
    'log2': [0.5, 1, 2, 10],
    'round': [-2.5, -0.5, 0, 0.5, 2.5],
    'sign': [-9223372036854775808, -7, -3.5, -0.0, 0, 5],
    'sin': [-10, -1, -0.5, 0, 0.5, 1, 10],
    'sinh': [-5, -1, 0, 1, 5],
    'sqrt': [0, 1, 2, 10],
    'tan': [-1, -0.5, 0, 0.5, 1],
    'tanh': [-5, -1, 0, 1, 5],
    'trunc': [-2.5, -0.5, 0, 0.5, 2.5],
}

# min/max ties on signed zeros are implementation-defined in C: the VM is
# compiled at -O0 (libm libcall) while the generated module is compiled at
# -O2, where the compiler may fold fmin/fmax to minnum/maxnum and change the
# sign of the zero.  Only non-tie pairs belong in the VM-equivalence check;
# argument order and signed-zero handling are covered by the adapter tests.
BINARY = {
    'max': [(1, 2), (3.5, 2), (-1, -2), (1, 1)],
    'min': [(1, 2), (3.5, 2), (-1, -2), (1, 1)],
    'pow': [(2, 3), (4, 0.5), (1, 10), (0.5, 2), (10, -1)],
    'fmod': [(7, 3), (7.5, 2), (-7, 3), (7, -3), (5, 2.5)],
    'hypot': [(3, 4), (1, 1), (0, 0), (5, 12), (10000000000, 10000000000)],
}

HOST_UNARY = '''#include "edges.c"
#include <assert.h>
#include <math.h>
#include <stdint.h>
int main(void) {
    const goat_native_module_v1_t *m = goat_native_query_v1(1);
    assert(m && m->entry_count == 2);
    for (unsigned i=0;i<m->entry_count;i++) {
        const goat_native_entry_v1_t *e=&m->entries[i];
        assert(e->parameter_count == 1 && e->return_type == GOAT_NATIVE_F64);
        goat_native_value_v1_t a={0},r={0}; a.type=e->parameter_types[0];
        if (a.type==GOAT_NATIVE_I64) {
            int64_t values[]={INT64_MIN,INT64_MAX,-2,-1,0,1,2};
            for (unsigned j=0;j<7;j++) {
                a.value.integer=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK);
                assert(r.type==GOAT_NATIVE_F64);
                volatile double expected=@@LIBM@@((double)values[j]);
                if (isnan(expected)) assert(isnan(r.value.real));
                else assert(r.value.real==expected);
            }
        } else {
            assert(a.type==GOAT_NATIVE_F64);
            double values[]={-0.0,0.0,-INFINITY,INFINITY,NAN,-3.5};
            for (unsigned j=0;j<6;j++) {
                a.value.real=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                volatile double expected=@@LIBM@@(values[j]);
                if (isnan(expected)) assert(isnan(r.value.real));
                else assert(r.value.real==expected);
            }
        }
    }
    return 0;
}
'''

HOST_BINARY = '''#include "edges.c"
#include <assert.h>
#include <math.h>
#include <stdint.h>
int main(void) {
    const goat_native_module_v1_t *m = goat_native_query_v1(1);
    assert(m && m->entry_count == 2);
    for (unsigned i=0;i<m->entry_count;i++) {
        const goat_native_entry_v1_t *e=&m->entries[i];
        assert(e->parameter_count == 2 && e->return_type == GOAT_NATIVE_F64);
        goat_native_value_v1_t a[2]={{0},{0}},r={0};
        a[0].type=e->parameter_types[0]; a[1].type=e->parameter_types[1];
        if (a[0].type==GOAT_NATIVE_I64) {
            assert(a[1].type==GOAT_NATIVE_I64);
            int64_t x[]={INT64_MIN,-2,-1,0,1,2,INT64_MAX}, y[]={2,3,2,2,3,2,2};
            for (unsigned j=0;j<7;j++) {
                a[0].value.integer=x[j]; a[1].value.integer=y[j];
                assert(e->invoke(1,2,a,&r)==GOAT_NATIVE_OK);
                assert(r.type==GOAT_NATIVE_F64);
                volatile double expected=@@LIBM@@((double)x[j],(double)y[j]);
                if (isnan(expected)) assert(isnan(r.value.real));
                else assert(r.value.real==expected);
            }
        } else {
            assert(a[1].type==GOAT_NATIVE_F64);
            double x[]={-0.0,0.0,-INFINITY,INFINITY,NAN,-3.5}, y[]={0.0,-0.0,2.0,2.0,1.0,2.0};
            for (unsigned j=0;j<6;j++) {
                a[0].value.real=x[j]; a[1].value.real=y[j];
                assert(e->invoke(1,2,a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                volatile double expected=@@LIBM@@(x[j],y[j]);
                if (isnan(expected)) assert(isnan(r.value.real));
                else assert(r.value.real==expected);
            }
        }
    }
    return 0;
}
'''


HOST_ABS = '''#include "edges.c"
#include <assert.h>
#include <math.h>
#include <stdint.h>
int main(void) {
    const goat_native_module_v1_t *m = goat_native_query_v1(1);
    assert(m && m->entry_count == 2);
    for (unsigned i=0;i<m->entry_count;i++) {
        const goat_native_entry_v1_t *e=&m->entries[i];
        assert(e->parameter_count == 1);
        goat_native_value_v1_t a={0},r={0}; a.type=e->parameter_types[0];
        if (a.type==GOAT_NATIVE_I64) {
            assert(e->return_type==GOAT_NATIVE_I64);
            int64_t values[]={INT64_MIN,INT64_MAX,-2,-1,0,1,2};
            for (unsigned j=0;j<7;j++) {
                a.value.integer=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK);
                int64_t expected=values[j]==INT64_MIN?INT64_MAX:(values[j]<0?-values[j]:values[j]);
                assert(r.type==GOAT_NATIVE_I64 && r.value.integer==expected);
            }
        } else {
            assert(a.type==GOAT_NATIVE_F64 && e->return_type==GOAT_NATIVE_F64);
            double values[]={-0.0,0.0,-INFINITY,INFINITY,NAN,-3.5};
            for (unsigned j=0;j<6;j++) {
                a.value.real=values[j];
                assert(e->invoke(1,1,&a,&r)==GOAT_NATIVE_OK && r.type==GOAT_NATIVE_F64);
                if (isnan(values[j])) assert(isnan(r.value.real));
                else assert(r.value.real==fabs(values[j]) && !signbit(r.value.real));
            }
        }
    }
    return 0;
}
'''


def main():
    goat, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GOAT_LANGUAGE='en')

    def run(*args):
        return subprocess.run([str(goat), *map(str, args)], env=env,
                              capture_output=True, text=True, timeout=30)

    def verify_native(name, source):
        path = output / (name + '.goat')
        path.write_text(source, encoding='utf-8')
        expected = run('--native', 'off', path)
        assert expected.returncode == 0 and expected.stdout, expected
        report = output / (name + '.report')
        result = run('--native', 'required', '--save-native', report, path)
        assert result.returncode == 0 and result.stdout == expected.stdout, (result, expected)
        fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
        assert int(fields['succeeded']) > 0 and fields['omitted'] == '0', fields
        assert fields['attempts'] == fields['succeeded'] and fields['retries'] == '0', fields

    for name, values in UNARY.items():
        body = ';'.join('println(f(%s))' % value for value in values)
        verify_native('unary-' + name, 'const f=func(n){return %s(n);};%s;' % (name, body))
        print('[ ok ] native libm', name)

    for name, pairs in BINARY.items():
        body = ';'.join('println(g(%s,%s))' % (a, b) for a, b in pairs)
        verify_native('binary-' + name, 'const g=func(a,b){return %s(a,b);};%s;' % (name, body))
        print('[ ok ] native libm', name)

    # Rebinding, aliasing and argument effects must retain VM semantics.
    for case, source in {
        'alias': 'const cosine=cos;const f=func(n){return cosine(n);};println(f(1));',
        'effects': 'const f=func(n){var x=n;var r=cos(x=x+1);return r*100+x;};println(f(1));',
        'effects_binary': 'const g=func(a,b){var x=a;var r=pow(x=x+1,b);return r+x;};'
                          'println(g(2,3));',
        'atan2': 'const g=func(a,b){return atan(a,b);};println(g(1,1));println(g(-1,1));'
                 'println(g(1,-1));println(g(2,1));',
        'atan_extra_arg': 'const f=func(n){var x=n;var r=atan(x=x+1,x=x+2,x=x+3);'
                          'return r*100+x;};println(f(1));',
        'abs_extra_arg': 'const f=func(n){var r=abs(n++,n++);return r*100+n;};println(f(-3));',
    }.items():
        verify_native('semantics-' + case, source)
        print('[ ok ] libm semantics', case)

    # Runtime rebinding and lexical shadowing cannot bind statically, so the
    # native layer must transparently fall back to the VM.
    for case, source in {
        'replace': 'const f=func(n){return cos(n);};println(f(1));'
                   'cos=func(n){return 99;};println(f(1));',
        'nested_shadow': 'const f=func(n){return cos(n);};const change=func(){'
                         'cos=func(n){return 88;};};change();println(f(1));',
    }.items():
        path = output / ('semantics-' + case + '.goat')
        path.write_text(source, encoding='utf-8')
        expected = run('--native', 'off', path)
        assert expected.returncode == 0 and expected.stdout, expected
        result = run('--native', 'auto', path)
        assert result.returncode == 0 and result.stdout == expected.stdout, result
        print('[ ok ] libm semantics', case)

    # Type errors and missing arguments must retain VM semantics.
    for case, source in {
        'atan_invalid': 'const f=func(n){return atan(n);};try {println(f("bad"));} '
                        'catch(e) {println("caught");}',
        'atan_missing': 'const f=func(){return atan();};try {println(f());} '
                        'catch(e) {println("caught");}',
        'abs_invalid': 'const f=func(n){return abs(n);};try {println(f("bad"));} '
                       'catch(e) {println("caught");}',
        'abs_missing': 'const f=func(){return abs();};try {println(f());} '
                       'catch(e) {println("caught");}',
    }.items():
        path = output / ('reject-' + case + '.goat')
        path.write_text(source, encoding='utf-8')
        expected = run('--native', 'off', path)
        assert expected.returncode == 0 and expected.stdout == 'caught\n', expected
        result = run('--native', 'auto', path)
        assert result.returncode == 0 and result.stdout == expected.stdout, result
        print('[ ok ] libm rejection', case)

    # Invoke the generated adapters directly with values absent from source observations.
    for name, libm, arity in [('cos', 'cos', 1), ('sqrt', 'sqrt', 1),
                               ('pow', 'pow', 2), ('fmod', 'fmod', 2),
                               ('min', 'fmin', 2), ('max', 'fmax', 2),
                               ('atan', 'atan2', 2), ('abs', 'abs', 3)]:
        if arity == 1:
            edges = 'const f=func(n){return %s(n);};f(1);f(1.0);' % name
            template = HOST_UNARY
        elif arity == 2:
            edges = 'const g=func(a,b){return %s(a,b);};g(1,1);g(1.0,1.0);' % name
            template = HOST_BINARY
        else:
            edges = 'const f=func(n){return %s(n);};f(1);f(1.0);' % name
            template = HOST_ABS
        # --save-c writes the module beside the source as <name>.c, and the
        # host templates include "edges.c", so a fixed source name is required.
        path = output / 'edges.goat'
        path.write_text(edges, encoding='utf-8')
        result = run('--save-c', path)
        assert result.returncode == 0, result
        host = output / ('host-' + name + '.c')
        host.write_text(template.replace('@@LIBM@@', libm), encoding='utf-8')
        exe = output / ('host-' + name + '.exe')
        compiled = subprocess.run([env.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                   '-O2', str(host), '-lm', '-o', str(exe)], capture_output=True,
                                  text=True, timeout=60)
        assert compiled.returncode == 0, compiled
        subprocess.run([str(exe)], check=True, timeout=30)
        print('[ ok ] %s adapters: INT64 boundaries, signed zero, NaN and infinities' % name)


if __name__ == '__main__':
    main()
