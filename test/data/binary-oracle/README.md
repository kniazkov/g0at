# Independent binary format oracle (#336)

This read-only corpus was captured from `reference.py` and
`module_interface_fixtures.py` at master `ce40f2b5c86833050fa3bdaf18385ee9b135b6ab`,
using CPython 3.12.14 / zlib 1.3.2. No golden vector was regenerated or changed.
The C oracle uses its own byte layout, SHA-256, bounds checks and ZIP records;
only bundled zlib's compression/CRC primitives and yyjson's DOM are shared.
It does not call Goat's bytecode, ZIP, hash, module validator or writer wrappers.

## Old-to-new coverage

| Python contract / old assertion | C API | Stable CTest ID |
|---|---|---|
| `minimal_bytecode`, `importing_bytecode`, `test_bytecode_layout` | `format_binary_minimal/importing/check` | `format.binary.bytecode`, `format.binary.vectors` |
| `check_module`, `test_cross_part_validation` | `format_binary_check_module` | `format.binary.modules` |
| `main_hash`, `test_hash_projection`, binary/vector SHA-256 | `format_binary_main_hash/sha256` | `format.binary.vectors` |
| `zip_bytes`, `test_archive_with_independent_reader` | `format_zip_build/inspect/extract` | `format.binary.zip`, `format.binary.vectors` |
| `test_native_envelope` | independent envelope checks | `format.binary.vectors` (JSON normalization also `format.oracle.native_envelope`) |
| `with_external_declarations` | `format_fixture_external_declarations` | `format.binary.fixtures` |
| deliberately wrong output / accept-invalid oracle | runner fault modes | `format.binary.reject-corrupt-output`, `format.binary.reject-accept-invalid` |
| same assertions under undefined-behaviour instrumentation | instrumented support and runner | `format.binary.ubsan` |

The schema snapshots are already checked by `module_schema_generator`; JSON,
normalization, guards and projection scenarios remain in `format.oracle.*`
(#335). Existing Python tests remain active, rather than losing any scenarios.

## Frozen corpus construction

* `bytecode.json`: 1,195 `{name,inputHex,valid}` rows. For each Python
  `minimal_bytecode()` and `importing_bytecode()`: the intact image; every strict
  prefix; every individual byte replaced by 0, 1 and 255 (excluding unchanged
  replacements); each uint32 header field at offsets 8..60 replaced with
  `0xffffffff`; one appended NUL. Also the eight declaration fixture images.
  `valid` is the result of `reference.check_bytecode`, with ValueError,
  UnicodeError and struct.error recorded as rejection. This includes accepted
  mutations, not an assumption that every changed byte is invalid.
* `fixtures.json`: nine `{name,metadata,inputHex,outputHex,expectedMetadata}`
  rows. Minimal and importing images, each with empty externals, immutable x,
  mixed mutable z/immutable a, and Unicode/NUL names (`Я😀`, U+E000, U+10000,
  `a\0b`). Python's helper supplies both the exact image and mutated metadata.
  The Unicode case checks UTF-32, scalar ordering and retained embedded NUL bytes.
  A ninth fixture preserves an existing ARG/FUNC block and function table.
* `modules.json`: 25 `{name,metadata,inputHex,valid}` rows. Empty/importing,
  wrong image, both math versions, invalid-entry vector, optimized-away imports,
  undeclared import, INCLUDE out of bounds, IMPORT absent descriptors,
  wrong hash, absent files and mismatched functions; valid FUNC records with id/file/instruction/body/arity/reserved mutations; retained FUNC indices after declaration append; malformed qualified IMPORT names. Hashes for modified images
  are recomputed with the Python reference before cross-part validation.

Hashes (SHA-256):

```
72341dae0345cfbfc3b995dfa08c8b98528a49e432b10c8528aeed0968c9996d  bytecode.json
fba4bbcbaecfefb2d8a96010cf900aa2797c34326189fe71ca001be808529032  fixtures.json
871646586033d6b604c8c6facc1a861c5fb51a63477901ad58c9dff5beb7821d  modules.json
```

## Reproduction

```sh
cmake -S . -B build/binary-oracle -DCMAKE_BUILD_TYPE=Debug
cmake --build build/binary-oracle --target format_binary_testing format_binary_ubsan_testing
ctest --test-dir build/binary-oracle -L format-binary --output-on-failure
python3 docs/module-format-v1/test_reference.py
python3 docs/module-format-v1/reference.py
```

Omit the UBSan target on Windows or musl without a sanitizer runtime. Normal
portable tests run on every existing Debug/Release Linux x86-64/AArch64 and
Windows MinGW i686/x86-64/UCRT CI matrix; UBSan additionally runs in glibc jobs.
A normal invocation only reads corpora, schema and vectors. Diagnostics and
failure files go through the shared scenario framework under the build tree.
No update flag is offered by this test runner.

## Retained Python consumers

`reference.py` and `test_reference.py` remain for their existing consumers (full
list in `test/support/README.md`), pending the ordered suite migrations and #394.
`scripts/module_interface_fixtures.py` remains used by
`check_module_graph.py`, `check_module_consistency.py`,
`check_module_validation.py`, `check_module_merge.py`,
and `check_module_inspect.py`.
Their API, paths and semantics are unchanged. The shared C replacements live in
`test/support/format_binary.{h,c}`, `format_zip.{h,c}` and
`format_fixtures.{h,c}`, in test-only `goat_format_oracle`.
