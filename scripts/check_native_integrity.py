#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Check SHA-256 binding and rejection before native library initialization."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys


def main():
    goat = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    extension = '.dll' if os.name == 'nt' else '.so'
    env = dict(os.environ, GOAT_LANGUAGE='en')

    def run(*args):
        return subprocess.run([str(goat), *map(str, args)], env=env,
                              capture_output=True, text=True, timeout=30)

    source = output / 'original.goat'
    source.write_text('const f=func(n){return n+1;};println(f(41));\n', encoding='utf-8')
    result = run('--compile', '--native', 'required', source)
    assert result.returncode == 0, result.stderr
    binary, library = source.with_suffix('.gbin'), source.with_suffix(extension)
    artifact, original = binary.read_bytes(), library.read_bytes()
    assert artifact[:8] == b'GOATBIN3'
    assert artifact[64:96] == hashlib.sha256(original).digest()
    assert run('--run', '--native', 'required', binary).stdout == '42\n'

    # Same IDs and numeric signature, different body: metadata alone cannot distinguish them.
    other = output / 'other.goat'
    other.write_text('const f=func(n){return n+2;};println(f(41));\n', encoding='utf-8')
    assert run('--compile', '--native', 'required', other).returncode == 0
    for changed in [other.with_suffix(extension).read_bytes(), original + b'changed']:
        library.write_bytes(changed)
        rejected = run('--run', '--native', 'required', binary)
        assert rejected.returncode != 0 and not rejected.stdout, rejected
        fallback = run('--run', '--native', 'auto', binary)
        assert fallback.returncode == 0 and fallback.stdout == '42\n', fallback

    # A library constructor would leave a marker even before query/metadata validation.
    assert run('--save-c', other).returncode == 0
    generated = other.with_suffix('.c')
    generated.write_text(generated.read_text(encoding='utf-8') + '''
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void marker(void) {
    const char *path = getenv("GOAT_INTEGRITY_MARKER");
    if (path) { FILE *f = fopen(path, "wb"); if (f) { fputs("loaded", f); fclose(f); } }
}
''', encoding='utf-8')
    poison = output / ('poison' + extension)
    command = [os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-shared',
               '-DGOAT_NATIVE_BUILD', str(generated), '-o', str(poison), '-lm']
    if os.name == 'nt':
        command += ['-static', '-static-libgcc', '-Wl,--exclude-all-symbols']
    else:
        command += ['-fPIC']
    subprocess.run(command, check=True, capture_output=True, timeout=60)
    marker = output / 'loaded.txt'
    marker.unlink(missing_ok=True)
    env['GOAT_INTEGRITY_MARKER'] = str(marker)
    library.write_bytes(poison.read_bytes())
    assert run('--run', '--native', 'required', binary).returncode != 0
    assert not marker.exists(), 'mismatched DLL was initialized'
    # Positive control: an explicitly paired copy does execute the constructor.
    paired = bytearray(artifact)
    paired[64:96] = hashlib.sha256(poison.read_bytes()).digest()
    paired[56:64] = bytes(8)
    checksum = 14695981039346656037
    for byte in paired:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    paired[56:64] = checksum.to_bytes(8, 'little')
    binary.write_bytes(paired)
    accepted = run('--run', '--native', 'required', binary)
    assert accepted.returncode == 0 and accepted.stdout == '43\n', accepted
    assert marker.read_text() == 'loaded'
    library.write_bytes(original)
    binary.write_bytes(artifact)
    assert run('--run', '--native', 'required', binary).stdout == '42\n'
    print('Native integrity checks passed: SHA-256, substitution, corruption, pre-load rejection')


if __name__ == '__main__':
    main()
