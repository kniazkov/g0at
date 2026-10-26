#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Validate actual loaded descriptor/symbol ownership before any module-v1 call."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

builder, runner = [str(Path(x).resolve()) for x in sys.argv[1:3]]
compiler = shutil.which(sys.argv[3]) or sys.argv[3]
count = 0
with tempfile.TemporaryDirectory(prefix='goat-module-library-') as temporary:
    root = Path(temporary)
    (root / 'main.goat').write_text('external square=func(x){return x*x;}; external add=func(x){return x+1;}; square(7); add(3);', encoding='utf-8')
    (root / 'module.gmod').write_text(json.dumps(dict(formatVersion=1, name='mapped', version='1', main='main.goat', native=dict(mode='required'))))
    run = subprocess.run([builder, 'native-build', str(root / 'module.gmod'), str(root / 'module.gbin'), 'descriptor', 'descriptor', compiler], capture_output=True, timeout=120)
    assert run.returncode == 0, (run.stdout, run.stderr)
    with zipfile.ZipFile(root / 'module.gbin') as archive:
        raw = archive.read('module.json')
        metadata = json.loads(raw)
        variant = metadata['native'][0]
        original = root / ('original.dll' if os.name == 'nt' else 'original.so')
        original.write_bytes(archive.read(variant['library']))
    manifest = root / 'module.json'
    manifest.write_bytes(raw)

    def check(library, success):
        global count
        result = subprocess.run([runner, str(library), str(manifest), 'success' if success else 'failure'], capture_output=True, timeout=30)
        assert result.returncode == 0, (library.name, result.returncode, result.stdout, result.stderr)
        count += 1

    check(original, True)
    descriptor = dict(formatVersion=1, module=metadata['name'], version=metadata['version'], mainHash=metadata['mainHash'], target=variant['target'], goatAbiVersion=variant['goatAbiVersion'], specializations=variant['specializations'])
    encoded = json.dumps(descriptor, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    envelope = b'GOATND01' + struct.pack('<Q', len(encoded)) + encoded + b'\0'
    names = [item['symbol'] for item in variant['specializations']]
    assert len(names) >= 2
    export = '__declspec(dllexport)' if os.name == 'nt' else '__attribute__((visibility("default")))'

    def alias_type_fixture(path, name, kind):
        # Compilers disagree about inline-assembly .type ordering for aliases.
        # Start with a real C function alias, prove its dynamic ELF identity,
        # and change only that exact alias's type for the adversarial fixture.
        # This is adversarial adapter metadata, not delivery execution evidence.
        image = bytearray(path.read_bytes())
        assert image[:6] == b"\x7fELF\x02\x01", "Linux test requires ELF64 little-endian"
        sections = struct.unpack_from('<Q', image, 40)[0]
        stride, amount = struct.unpack_from('<HH', image, 58)
        assert stride >= 64 and amount and sections + stride * amount <= len(image)

        def section(index):
            assert index < amount
            return struct.unpack_from('<IIQQQQIIQQ', image, sections + index * stride)

        found = {}
        for index in range(amount):
            table = section(index)
            if table[1] != 11:  # SHT_DYNSYM
                continue
            offset, size, link, step = table[4], table[5], table[6], table[9]
            assert step == 24 and size % step == 0 and offset + size <= len(image)
            strings = section(link)
            assert strings[1] == 3 and strings[4] + strings[5] <= len(image)
            for entry in range(offset, offset + size, step):
                name_offset, info, _, defined, address, _ = struct.unpack_from('<IBBHQQ', image, entry)
                assert name_offset < strings[5]
                first = strings[4] + name_offset
                end = image.find(b'\0', first, strings[4] + strings[5])
                assert end >= first
                symbol = bytes(image[first:end]).decode('utf-8')
                if symbol in (name, 'actual_function'):
                    assert symbol not in found and defined and info & 15 == 2
                    found[symbol] = (entry, address, defined)
        assert set(found) == {name, 'actual_function'}, found
        assert found[name][1:] == found['actual_function'][1:] and found[name][1]
        function_entry = found['actual_function'][0]
        function_bytes = bytes(image[function_entry:function_entry + 24])
        if kind == 'object':
            entry = found[name][0]
            image[entry + 4] = (image[entry + 4] & 0xf0) | 1  # STT_OBJECT
            path.write_bytes(image)
        assert image[found[name][0] + 4] & 15 == (1 if kind == 'object' else 2)
        assert image[function_entry + 4] & 15 == 2
        assert bytes(image[function_entry:function_entry + 24]) == function_bytes
        assert path.read_bytes() == image

    def build(name, data=envelope, qualifier='const', symbols=None, extra='', missing=False, data_symbol=False, data_in_text=False, hash_style=None, symbol_alias=None):
        source = '' if missing else f'{export} {qualifier} unsigned char goat_module_descriptor_v1[] = {{{",".join(map(str, data))}}};\n'
        data_attribute = '__attribute__((section(".text")))' if data_in_text else ''
        for index, symbol in enumerate(names if symbols is None else symbols):
            if index == 0 and symbol_alias:
                source += f'{export} unsigned actual_function(void) {{ return 0; }}\n'
                source += f'{export} unsigned {symbol}(void) __attribute__((alias("actual_function")));\n'
                continue
            source += f'{export} {data_attribute} const int {symbol}=0;\n' if data_symbol and index == 0 else f'{export} unsigned {symbol}(void) {{ return 0; }}\n'
        source += extra
        path = root / (name + ('.dll' if os.name == 'nt' else '.so'))
        code = root / (name + '.c')
        code.write_text(source, encoding='utf-8')
        command = [compiler, '-shared', '-o', str(path), str(code)]
        if os.name != 'nt':
            command.insert(1, '-fPIC')
            if hash_style:
                command.append('-Wl,--hash-style=' + hash_style)
        result = subprocess.run(command, capture_output=True, timeout=60)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        if symbol_alias:
            alias_type_fixture(path, names[0], symbol_alias)
        return path

    check(build('valid'), True)
    check(build('missing-descriptor', missing=True), False)
    check(build('writable-descriptor', qualifier=''), False)
    check(build('bad-magic', data=b'INVALID!' + envelope[8:]), False)
    check(build('overflow', data=b'GOATND01' + b'\xff' * 8), False)
    check(build('outside-image', data=b'GOATND01' + struct.pack('<Q', 1 << 30)), False)
    check(build('unterminated', data=envelope[:-1] + b'!'), False)
    check(build('different-name', data=envelope.replace(b'mapped', b'otherx')), False)
    check(build('missing-last-symbol', symbols=names[:-1]), False)
    check(build('data-symbol', data_symbol=True), False)
    if os.name != 'nt':
        check(build('data-in-text', data_symbol=True, data_in_text=True), False)
        # musl lacks dladdr1; portable validation must derive bounded dynamic
        # symbols from either supported ELF hash table without accepting data
        # merely because its address lies in executable memory.
        for hash_style in ('sysv', 'gnu', 'both'):
            check(build('valid-' + hash_style, hash_style=hash_style), True)
            check(build('data-in-text-' + hash_style, data_symbol=True,
                        data_in_text=True, hash_style=hash_style), False)
            check(build('object-alias-' + hash_style, symbol_alias='object',
                        hash_style=hash_style), False)
            check(build('function-alias-' + hash_style, symbol_alias='function',
                        hash_style=hash_style), True)
        hidden = '#include <sys/mman.h>\n__attribute__((constructor)) static void protect_descriptor(void) { mprotect((void *)goat_module_descriptor_v1, 4096, PROT_NONE); }\n'
        check(build('unreadable-descriptor', data=envelope + b'\0' * 8192,
                    qualifier='const __attribute__((aligned(4096)))', extra=hidden), False)
        # A lookup can succeed in a dependency: it must not establish ownership.
        dependency = build('dependency', symbols=[], extra='int dependency_marker(void) { return 1; }\n')
        code = root / 'foreign.c'
        code.write_text('extern int dependency_marker(void);\n' + '\n'.join(f'{export} unsigned {name}(void) {{ return dependency_marker(); }}' for name in names))
        foreign = root / 'foreign.so'
        result = subprocess.run([compiler, '-shared', '-fPIC', '-o', str(foreign), str(code), str(dependency), '-Wl,-rpath,' + str(root)], capture_output=True, timeout=60)
        assert result.returncode == 0, result.stderr
        check(foreign, False)
print(f'Mapped module library checks passed: {count}')
