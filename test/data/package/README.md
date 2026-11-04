# Package relocation fixtures (#391)

`sdk.c.txt` is the exact UTF-8 string literal passed to `probe_source.write_text` in
`scripts/check_package.py` at base `f97e0388`. Before retirement, the temporary
parity wrapper verified AST correspondence and raw SHA-256 for both inputs.
The fixture keeps `-text` in `.gitattributes` to preserve exact bytes on Windows.
The original and temporary wrapper remain available in Git at parity head
`47293b59f31036b771530655dca153866b49924b`.

SDK SHA-256: `a2ab9a59cbe81ec42a659d33a84906208699d01823833e71d4665ffd482b03ae`
Original SHA-256: `2e0f1e3a69fe7b2722590044210d59e45d984f133d89c2a34f811e8151ad3243`

During parity both implementations unpacked the same produced archive independently.
The C runner continues to relocate it,
exercise interpreter/bytecode/native/string/numeric/SDK checks, and run the full
module-delivery end-to-end suite using relocated tools with its 300-second budget.
Archive enumeration is sorted ZIP files followed by sorted tar.gz files.
Original Python and the wrapper were retired after [PR #472](https://github.com/kniazkov/g0at/pull/472)
passed [Build 11/11](https://github.com/kniazkov/g0at/actions/runs/41857101812) and
[vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41857101772). All nine
platform contexts passed actual Debug/Release parity, including three Windows
ZIP contexts and both native musl architectures.

## Scenario correspondence

| Original check | C scenario / coverage |
| --- | --- |
| ZIP or tar.gz argument; sorted package-directory enumeration | `integration.package.end-to-end` |
| Unpack one root; relocate outside the checkout; required SDK/runtime files | `integration.package.end-to-end` |
| Inspector help; compiler-free hello; native string identity/concatenation and quadratic counters | `integration.package.end-to-end` |
| Numeric VM; compile bytecode without optimization; remove source and execute bytecode | `integration.package.end-to-end` |
| Compile printed C using installed headers; link and execute frozen external-symbol SDK probe | `integration.package.end-to-end` |
| Exported static archive and three headers match installed bytes | `integration.package.end-to-end` |
| Compile native pair; delete source; relocate again; require native preparation and execution | `integration.package.end-to-end` |
| Full module-v1 delivery using relocated goat/inspect/merge | Nested `integration.module_delivery.end-to-end`, unchanged 300-second limit |
| Corrupt archive/path and counter rejection | `integration.package.controls` |

CTest `package.relocated` directly selects `integration.package.end-to-end`.
The explicit CI package step runs all produced release archives with both Debug
and Release runners. An absent package directory disables only the optional
CTest package invocation, not the explicit CI package step. Controls run without
a release archive and do not depend on Python, `assert`, or `NDEBUG`.

## Reproduction

Build `package_scenarios` and the release `package` target, then invoke (one
archive or a directory is accepted):

```sh
build/package_scenarios integration.package.end-to-end \
  build/release/packages build/module_delivery_scenarios \
  "$(command -v gcc)" "$PWD" "$(command -v cmake)" --timeout-seconds 1800 --keep-work
```

Extraction uses the explicitly supplied CMake executable and its archive support,
not a shell or Python. Archive names and types are checked before extraction.
The original command budgets remain 15 seconds for inspector help, 90 seconds
for Goat/compiler commands, 30 seconds for the SDK executable, and 300 seconds
for nested module delivery. Failures, process cleanup errors and timeouts fail
closed. Package payload execution stays outside the checkout, with spaces in
paths; the source fixtures are read-only inputs. No language, binary-format,
compiler optimization or native-success criterion is relaxed by this migration.
