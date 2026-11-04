# Package relocation fixtures (#391)

`sdk.c` is the exact UTF-8 string literal passed to `probe_source.write_text` in
`scripts/check_package.py` at base `f97e0388`. The temporary parity wrapper checks
AST correspondence and SHA-256 before running either implementation.

SDK SHA-256: `a2ab9a59cbe81ec42a659d33a84906208699d01823833e71d4665ffd482b03ae`
Original SHA-256: `2e0f1e3a69fe7b2722590044210d59e45d984f133d89c2a34f811e8151ad3243`

Both implementations unpack the same produced archive independently, relocate it,
exercise interpreter/bytecode/native/string/numeric/SDK checks, and run the full
module-delivery end-to-end suite using relocated tools with its 300-second budget.
Archive enumeration is sorted ZIP files followed by sorted tar.gz files.
Temporary Python parity remains until the complete 14-check acceptance gate.

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

The temporary CTest `package.relocated` runs the Python/C parity wrapper; the
explicit CI package step runs it against each produced release archive for both
Debug and Release runners. An absent package directory disables only the optional
CTest package invocation, not the explicit CI package step. Controls run without
a release archive and do not depend on `assert` or `NDEBUG`.

## Reproduction during parity

Build `package_scenarios` and the release `package` target, then invoke:

```sh
python3 scripts/check_package_parity.py build/release/packages \
  build/package_scenarios build/module_delivery_scenarios \
  gcc "$PWD" "$(command -v cmake)"
```

Direct C invocation (also accepts one archive):

```sh
build/package_scenarios integration.package.end-to-end \
  build/release/packages build/module_delivery_scenarios \
  gcc "$PWD" "$(command -v cmake)" --timeout-seconds 1800 --keep-work
```

Extraction uses the explicitly supplied CMake executable and its archive support,
not a shell or Python. Archive names and types are checked before extraction.
The original command budgets remain 15 seconds for inspector help, 90 seconds
for Goat/compiler commands, 30 seconds for the SDK executable, and 300 seconds
for nested module delivery. Failures, process cleanup errors and timeouts fail
closed. Package payload execution stays outside the checkout, with spaces in
paths; the source fixtures are read-only inputs. No language, binary-format,
compiler optimization or native-success criterion is relaxed by this migration.
