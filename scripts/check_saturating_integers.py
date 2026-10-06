#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Check saturation against exact Python integers, including portable generated C."""
import itertools
import os
from pathlib import Path
import random
import subprocess
import sys

MIN = -(1 << 63)
MAX = (1 << 63) - 1


def clamp(value):
    return min(MAX, max(MIN, value))


def literal(value):
    return '(-INT64_C(9223372036854775807)-1)' if value == MIN else (
        '-INT64_C(%d)' % -value if value < 0 else 'INT64_C(%d)' % value)


def main():
    goat, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    root = Path(__file__).resolve().parent.parent
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GOAT_LANGUAGE='en')

    def run(args, expected=None):
        result = subprocess.run(list(map(str, args)), env=env, capture_output=True,
                                text=True, timeout=120)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        if expected is not None:
            assert result.stdout == expected, (args, result.stdout, expected)
        return result.stdout

    edges = [MIN, MIN + 1, MIN + 2, -3037000500, -2, -1, 0, 1, 2,
             3037000499, 3037000500, MAX - 2, MAX - 1, MAX]
    pairs = list(itertools.product(edges, repeat=2))
    rng = random.Random(20261006)
    pairs += [(rng.randint(MIN, MAX), rng.randint(MIN, MAX)) for _ in range(1000)]
    definitions = ('const add=func(a,b){return a+b;};'
                   'const sub=func(a,b){return a-b;};'
                   'const mul=func(a,b){return a*b;};'
                   'const neg=func(a){return -a;};'
                   'const mag=func(a){return abs(a);};'
                   'const inc=func(a){return ++a;};'
                   'const dec=func(a){return --a;};'
                   'const divmin=func(a){return (-9223372036854775808/-1)+a;};')
    lines = [definitions]
    expected = []
    for a, b in pairs[:len(edges)**2]:
        for name, value in [('add', a+b), ('sub', a-b), ('mul', a*b)]:
            lines.append('println(%s(%d,%d));' % (name, a, b))
            expected.append(str(clamp(value)))
    for a in edges:
        for name, value in [('neg', -a), ('mag', abs(a)), ('inc', a+1), ('dec', a-1), ('divmin', MAX+a)]:
            lines.append('println(%s(%d));' % (name, a))
            expected.append(str(clamp(value)))
    source = output / 'saturation.goat'
    source.write_text('\n'.join(lines), encoding='utf-8')
    expected = '\n'.join(expected) + '\n'
    for optimize, native in [('none', 'off'), ('all', 'off'), ('all', 'auto'), ('all', 'required')]:
        report = output / (optimize + '-' + native + '.report')
        run([goat, '--optimize', optimize, '--native', native, '--save-native', report, source], expected)
        if native != 'off':
            fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
            assert int(fields['succeeded']) > 0 and fields['attempts'] == fields['succeeded'], fields
            assert fields['retries'] == '0', fields
    run([goat, '--compile', '--native', 'required', source])
    run([goat, '--run', '--native', 'required', source.with_suffix('.gbin')], expected)
    print('[ ok ] VM, optimization, native dispatch and saved bytecode saturation')

    module = source.with_suffix('.c')
    run([goat, '--save-c', source])
    # Compare both VM helper implementations and generated C helpers with an independent oracle.
    checks = ['#include "saturation.c"', '#include "integer_math.h"', '#include <assert.h>',
              'int main(void) {']
    for a, b in pairs:
        checks.append('{ volatile int64_t a=%s,b=%s;' % (literal(a), literal(b)))
        for op, helper, value in [('add', 'add', a+b), ('sub', 'subtract', a-b),
                                   ('mul', 'multiply', a*b)]:
            args = 'a,b'
            result = literal(clamp(value))
            checks.append('assert(g_i64_%s(%s)==%s);' % (op, args, result))
            checks.append('assert(%s_int64_saturating(%s)==%s);' % (helper, args, result))
        checks.append('}')
    checks += ['return 0;', '}']
    driver = output / 'driver.c'
    driver.write_text('\n'.join(checks), encoding='utf-8')
    for portable in [False, True]:
        executable = output / ('portable.exe' if portable else 'builtin.exe')
        flags = ['-DGOAT_FORCE_PORTABLE_INTEGER_MATH'] if portable else []
        run([os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             *flags, '-I' + str(root / 'src/lib'), driver, '-lm', '-o', executable])
        run([executable])
    print('[ ok ] exact oracle: %d operand pairs, intrinsics and portable C' % len(pairs))

    valid = {'minimum': ('println(-9223372036854775808);', str(MIN)+'\n'),
             'spaced_minimum': ('println(- 9223372036854775808);', str(MIN)+'\n'),
             'double_negation': ('println(-(-9223372036854775808));', str(MAX)+'\n'),
             'division': ('println((-9223372036854775808/-1)%2);'
                          'println(-9223372036854775808%-1);println(3/2);', '1\n0\n1.5\n'),
             'order': ('println((9223372036854775807+1)-1);'
                       'println(9223372036854775807+(1-1));', '%d\n%d\n' % (MAX-1, MAX)),
             'unchanged': ('println(9223372036854775807<<1);println(2**3);'
                           'println(9223372036854775808.0>0);', '-2\n8.0\ntrue\n')}
    for name, (text, answer) in valid.items():
        path = output / (name + '.goat')
        path.write_text(text, encoding='utf-8')
        for optimize in ['none', 'all']:
            run([goat, '--optimize', optimize, path], answer)
    for i, text in enumerate(['9223372036854775808', '-9223372036854775809',
                              '18446744073709551616', '9'*200,
                              '+9223372036854775808', '1-9223372036854775808',
                              '-(9223372036854775808)', '-9223372036854775808**1']):
        path = output / ('invalid%d.goat' % i)
        path.write_text('println(%s);' % text, encoding='utf-8')
        result = subprocess.run([str(goat), str(path)], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode != 0 and '64-bit' in result.stderr, (text, result)
    print('[ ok ] literal bounds, exact division, evaluation order and unchanged real/bitwise rules')


if __name__ == '__main__':
    main()
