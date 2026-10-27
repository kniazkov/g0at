# Native Unicode 17.0.0 generator

`unicode_generator` replaces `scripts/generate_unicode.py` (#333). It reads
vendored `third_party/unicode/17.0.0` UCD files without network access and writes
`lib/strings/unicode_data.h` and `third_party/unicode/17.0.0/SHA256SUMS` only on
an explicit `--update`. Ordinary builds consume committed tables.

```sh
cmake -S . -B build/native
cmake --build build/native --target unicode_generator unicode_generator_testing
build/native/unicode_generator --check
ctest --test-dir build/native --output-on-failure -L unicode-generator
# Explicit, reviewable update:
build/native/unicode_generator --update
```

`--root PATH` selects another repository/output tree; the default is the checkout
recorded at configure time, independent of the current directory. Check mode is
read-only and fails on stale or missing outputs. Outputs use LF; text comparison
accepts CRLF checkouts, while SHA-256 covers exact input bytes. `.gitattributes`
keeps vendored UCD text LF on Windows so the committed manifest is portable.
Unknown arguments, invalid code points, malformed relevant records, and missing
inputs are errors. All input parsing completes before output files are opened.

## Host tools and cross compilation

The generator uses only its own C source and `lib/hash/sha256.c`, not the common
library, interpreter, Python, or the generated Unicode header. Build it alone:

```sh
cmake -S tools/unicode -B build/unicode-host
cmake --build build/unicode-host --config Release
ctest --test-dir build/unicode-host -C Release --output-on-failure
```

For cross compilation configure that standalone directory with a **host** C
compiler in a separate build directory and run the host executable. Target builds
never execute the generator. Multi-configuration builds place the executable in
`Release/`; Windows adds `.exe`.

## Migration verification

The old Python generator and C generator are run on the same vendored inputs.
The table body and complete manifest must be byte-identical; only the generated
header's first-line tool attribution changes. Simple upper/lower mappings come
from UnicodeData; unconditional SpecialCasing overrides the full mappings.
Conditional and locale-specific mappings remain excluded. Cased, Case_Ignorable,
and White_Space ranges retain the old ordering. The header intentionally has no
`#pragma once` or include guard and remains implementation-only.

| Old contract / additional guard | CTest ID |
| --- | --- |
| All four mapping tables, three properties, and SHA256SUMS match committed data | `unicode.generator.check` |
| Explicit regeneration and repeated checks are deterministic | `unicode.generator.roundtrip` |
| Corrupt header or manifest is rejected without rewriting it | `unicode.generator.stale` (each output independently) |
| Missing artifacts/input fail clearly | `unicode.generator.missing`, `unicode.generator.missing-input` (each file independently) |
| Invalid UCD is rejected before output mutation | `unicode.generator.reject-unicode`, `unicode.generator.reject-special`, `unicode.generator.reject-property` |

Regression scenarios use the common scenario framework: unique build-tree
workspaces, shared deadline, process diagnostics, cleanup on success, and retained
failure evidence. Linux and Windows Debug/Release jobs run the labeled suite;
the format-vector matrix checks the standalone host target as well. Existing
Python Unicode behavior tests remain unchanged until #340.
