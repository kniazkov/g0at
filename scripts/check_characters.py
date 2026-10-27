#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Character Unicode, source round trips, and native uint32 storage regressions."""
import os
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    goat, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(*args):
        result = subprocess.run([str(goat), *map(str, args)], capture_output=True,
                                text=True, encoding='utf-8', timeout=30,
                                env=dict(os.environ, GOAT_LANGUAGE='en'))
        assert result.returncode == 0, result
        return result.stdout

    source = output / 'unicode.goat'
    source.write_text("const zero=chr(0); println(ord(zero));\n"
                      "const c='😀'; println(c); println(c + \"x\");\n"
                      "println(c + \"\" == \"😀\"); println(ord(c));\n"
                      "const high=chr(4294967295); println(ord(high));\n",
                      encoding='utf-8')
    expected = '0\n😀\n😀x\ntrue\n128512\n4294967295\n'
    for optimize in ('none', 'all'):
        assert run('--optimize', optimize, source) == expected
        regenerated = output / ('regenerated-' + optimize + '.goat')
        regenerated.write_text(run('--compile', '--optimize', optimize,
                                   '--print-source-code', source), encoding='utf-8')
        assert run(regenerated) == expected
        run('--compile', '--optimize', optimize, source)
        assert run('--run', '--native', 'off', source.with_suffix('.gbin')) == expected

    native = output / 'native.goat'
    values = [0, 65, 65535, 65536, 128512, 1114111, 2147483648, 4294967295, -1]
    native.write_text('const f=func(n){return ord(chr(n));};\n' +
                      ''.join(f'println(f({n}));\n' for n in values), encoding='utf-8')
    expected = ''.join(f'{n & 0xffffffff}\n' for n in values)
    assert run('--optimize', 'none', native) == expected
    report = output / 'native.report'
    assert run('--native', 'required', '--save-native', report, native) == expected
    fields = dict(line.split('=', 1) for line in report.read_text().splitlines())
    assert int(fields['succeeded']) == len(values) and fields['retries'] == '0', fields
    run('--compile', '--native', 'required', native)
    assert run('--run', '--native', 'required', native.with_suffix('.gbin')) == expected
    run('--save-c', native)
    host = output / 'host.c'
    host.write_text('''#include "native.c"
#include <assert.h>
#include "test_allocator.h"
int main(void) {
    goat_native_context_v2_t allocator = test_context();
    const goat_native_module_v2_t *m = goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    assert(m && m->entry_count == 1);
    int64_t values[] = {0, 65, 65535, 65536, 128512, 1114111, 2147483648LL, 4294967295LL, -1};
    for (unsigned i=0; i<sizeof(values)/sizeof(*values); i++) {
        goat_native_value_v2_t a={0}, r={0};
        a.type=GOAT_NATIVE_I64; a.value.integer=values[i];
        assert(m->entries[0].invoke(GOAT_NATIVE_ABI_VERSION,&allocator,1,&a,&r)==GOAT_NATIVE_OK);
        assert(r.type==GOAT_NATIVE_I64 && r.value.integer==(int64_t)(uint32_t)values[i]);
    }
    return 0;
}
''', encoding='utf-8')
    for flags, name in [([], 'host'), (['-fshort-wchar'], 'short-wchar')]:
        executable = output / (name + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([*shlex.split(os.environ.get('CC', 'gcc')), '-std=c11', '-I' + str(Path(__file__).resolve().parent.parent / 'include'),
                         '-I' + str(Path(__file__).resolve().parent.parent / 'test/src/functional/native_abi'),
                        *flags, str(host), '-o', str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=30)
    print('[ ok ] characters: Unicode, source/binary round trips, VM/native, 16-bit system wchar_t')


if __name__ == '__main__':
    main()
