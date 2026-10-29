# Multifile access acceptance fixtures (AC-01–AC-28)

Copyright 2026 Ivan Kniazkov. Distributed under the repository MIT license.

`scenarios.json` is the single read-only table of source files, module descriptors,
ordered host operations, expected stdout and required rejection diagnostics for
[#359](https://github.com/kniazkov/g0at/issues/359). It preserves the concrete
expansion of `module_access_basic_cases.py`, `module_access_copy_cases.py`, and
`module_access_boundary_cases.py` at commit
`5efafe2fa98dfeabf8a7b6995ad96cf915b3f977`. No random inputs are involved.

The data contains **82 independent projects**, **228 compile/VM operations**, and
**318 additional public CLI operations**. The C runner is
[`module_multifile_access_scenarios.c`](../../src/functional/module_multifile_access_scenarios.c).
It uses the existing `test/support/module_execution` and scenario framework; it
does not depend on the Python helpers. Those helpers had no other consumers and
are removed together. `module_execution_support.py` remains only for unrelated
pending migrations, listed in `test/support/README.md` (#394).

## Preserved observations

- Basic declarations, constants, modifier syntax, static denials inside `try`,
  dynamic dot/index denials, moved methods, closures, captured file domains,
  protected descendants and hidden lookup barriers (AC-01–AC-10).
- Both include and compiled reexport boundaries; clone/flat variants, private
  and protected variants, local/shared cells, shallow nested values, method
  identity and prototype graphs (AC-11–AC-22).
- Observer-specific formatting/keys, exceptional callbacks, recursive values,
  key snapshots, array aliasing, factories, ownership and provenance, protected
  access, constness and foreign extension, including indexed assignment
  (AC-23–AC-28).
- Every successful app is executed by the host from its persisted binary, then
  by the public CLI from both source and binary, with `--native off` and both
  `--optimize none` / `all`, from an unrelated working directory with a private
  `GOAT_MODULE_PATH`. Expected stdout is exact except CRLF normalization.
- Every static rejection checks normal nonzero host/CLI exit, nonempty stderr,
  its specific diagnostic, no emitted artifact, and both CLI optimizers.
  Crashes, launch errors and timeouts cannot count as expected rejection.
- Each ID uses its own unique build directory and a shared 120-second deadline
  (CTest permits 150 seconds for cleanup). Failed runs retain commands,
  streams and fixtures; successful runs remove their directory unless
  `--keep-work` is selected. The input table is never modified by the runner.
- No platform SKIP branches: Linux x86-64/AArch64 GCC/Clang, Windows
  MinGW i686/x86-64/UCRT and musl run the same IDs. Existing Debug and Release
  calls are retained, and Release assertions remain enabled in the host.

## Reproduction

```sh
cmake -S . -B build/access-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/access-debug --parallel 4 --target module_multifile_access_scenarios
ctest --test-dir build/access-debug --output-on-failure --parallel 4 -L module-multifile-access-migration
# Repeat with -DCMAKE_BUILD_TYPE=Release and another build directory.
build/access-debug/module_multifile_access_scenarios integration.module_multifile_access.ac-01-declarations-include build/access-debug/module_import_execution_testing build/access-debug/goat build/access-debug test/data test/data --keep-work
# Replace the ID by --list to list all concrete scenarios.
```

The final argument before `--keep-work` is the fixture root, normally `test/data`;
it can point at a copied table for negative checks without modifying source data.

Verification before removing Python:

- Debug and Release: original Python runner and all 82 C IDs pass on the same
  hosts; both implementations execute 228 host and 318 public CLI checks.
- Captured the *executed* Python projects and compared every source/descriptor
  byte and ordered operation, expected stdout and diagnostic with the table.
  A separate independent capture of the original helpers also matches all 82.
- UBSan: all 82 IDs pass with `-fsanitize=undefined -fno-sanitize-recover=all`.
- Negative Release runs: a copied table with wrong stdout, a missing required
  diagnostic, and malformed JSON each return failure. Scenario failures retain
  diagnostics. The source fixture SHA-256 remains unchanged.

The original runner can be reproduced by extracting the four replaced files
from the provenance commit into `build/access-python/`, setting `PYTHONPATH` to
the current `scripts/` directory for its unchanged shared execution helper, and
running `python3 build/access-python/check_module_multifile_access.py HOST GOAT`.
No Python extraction or generation is needed for normal CTest runs.

## Exact scenario map

The legacy project name is retained (lowercased) as the suffix of each CTest ID.
Rows cover all loop expansions; related assertions stay together within one
project. `basic`, `copy`, and `boundary` identify the former corresponding
`module_access_*_cases.py` file. CMake reads IDs from this same table rather than
maintaining an independent registration list.

| Legacy group / project | New CTest ID | Host / CLI checks |
|---|---|---:|
| basic / `ac-01-declarations-include` | `integration.module_multifile_access.ac-01-declarations-include` | 2 / 4 |
| basic / `ac-01-declarations-import` | `integration.module_multifile_access.ac-01-declarations-import` | 3 / 4 |
| basic / `ac-02-syntax-0` | `integration.module_multifile_access.ac-02-syntax-0` | 1 / 2 |
| basic / `ac-02-syntax-1` | `integration.module_multifile_access.ac-02-syntax-1` | 1 / 2 |
| basic / `ac-02-syntax-2` | `integration.module_multifile_access.ac-02-syntax-2` | 1 / 2 |
| basic / `ac-02-assignment-keeps-modifier-include` | `integration.module_multifile_access.ac-02-assignment-keeps-modifier-include` | 2 / 4 |
| basic / `ac-02-assignment-keeps-modifier-import` | `integration.module_multifile_access.ac-02-assignment-keeps-modifier-import` | 3 / 4 |
| basic / `ac-03-body-dot-index-include` | `integration.module_multifile_access.ac-03-body-dot-index-include` | 2 / 4 |
| basic / `ac-03-body-dot-index-import` | `integration.module_multifile_access.ac-03-body-dot-index-import` | 3 / 4 |
| basic / `ac-03-static-0` | `integration.module_multifile_access.ac-03-static-0` | 1 / 2 |
| basic / `ac-03-static-1` | `integration.module_multifile_access.ac-03-static-1` | 1 / 2 |
| basic / `ac-04-clone-include` | `integration.module_multifile_access.ac-04-clone-include` | 2 / 4 |
| basic / `ac-04-clone-import` | `integration.module_multifile_access.ac-04-clone-import` | 3 / 4 |
| basic / `ac-04-flat-include` | `integration.module_multifile_access.ac-04-flat-include` | 2 / 4 |
| basic / `ac-04-flat-import` | `integration.module_multifile_access.ac-04-flat-import` | 3 / 4 |
| basic / `ac-05-ordinary-method-move-include` | `integration.module_multifile_access.ac-05-ordinary-method-move-include` | 2 / 4 |
| basic / `ac-05-ordinary-method-move-import` | `integration.module_multifile_access.ac-05-ordinary-method-move-import` | 3 / 4 |
| basic / `ac-06-private-include` | `integration.module_multifile_access.ac-06-private-include` | 2 / 4 |
| basic / `ac-06-private-import` | `integration.module_multifile_access.ac-06-private-import` | 3 / 4 |
| basic / `ac-06-protected-include` | `integration.module_multifile_access.ac-06-protected-include` | 2 / 4 |
| basic / `ac-06-protected-import` | `integration.module_multifile_access.ac-06-protected-import` | 3 / 4 |
| basic / `ac-07-escaped-closure-include` | `integration.module_multifile_access.ac-07-escaped-closure-include` | 2 / 4 |
| basic / `ac-07-escaped-closure-import` | `integration.module_multifile_access.ac-07-escaped-closure-import` | 3 / 4 |
| basic / `ac-08-nested-capture-include` | `integration.module_multifile_access.ac-08-nested-capture-include` | 2 / 4 |
| basic / `ac-08-nested-capture-import` | `integration.module_multifile_access.ac-08-nested-capture-import` | 3 / 4 |
| basic / `ac-09-descendant-rights-include` | `integration.module_multifile_access.ac-09-descendant-rights-include` | 2 / 4 |
| basic / `ac-09-descendant-rights-import` | `integration.module_multifile_access.ac-09-descendant-rights-import` | 3 / 4 |
| basic / `ac-10-hidden-shadow-clone-include` | `integration.module_multifile_access.ac-10-hidden-shadow-clone-include` | 2 / 4 |
| basic / `ac-10-hidden-shadow-clone-import` | `integration.module_multifile_access.ac-10-hidden-shadow-clone-import` | 3 / 4 |
| basic / `ac-10-hidden-shadow-flat-include` | `integration.module_multifile_access.ac-10-hidden-shadow-flat-include` | 2 / 4 |
| basic / `ac-10-hidden-shadow-flat-import` | `integration.module_multifile_access.ac-10-hidden-shadow-flat-import` | 3 / 4 |
| copy / `AC-11-include` | `integration.module_multifile_access.ac-11-include` | 2 / 4 |
| copy / `AC-11-import` | `integration.module_multifile_access.ac-11-import` | 4 / 4 |
| copy / `AC-12-include` | `integration.module_multifile_access.ac-12-include` | 2 / 4 |
| copy / `AC-12-import` | `integration.module_multifile_access.ac-12-import` | 4 / 4 |
| copy / `AC-13-include` | `integration.module_multifile_access.ac-13-include` | 2 / 4 |
| copy / `AC-13-import` | `integration.module_multifile_access.ac-13-import` | 4 / 4 |
| copy / `AC-14-clone-include` | `integration.module_multifile_access.ac-14-clone-include` | 2 / 4 |
| copy / `AC-14-clone-import` | `integration.module_multifile_access.ac-14-clone-import` | 4 / 4 |
| copy / `AC-15-clone-include` | `integration.module_multifile_access.ac-15-clone-include` | 2 / 4 |
| copy / `AC-15-clone-import` | `integration.module_multifile_access.ac-15-clone-import` | 4 / 4 |
| copy / `AC-16-clone-include` | `integration.module_multifile_access.ac-16-clone-include` | 2 / 4 |
| copy / `AC-16-clone-import` | `integration.module_multifile_access.ac-16-clone-import` | 4 / 4 |
| copy / `AC-17-clone-private-include` | `integration.module_multifile_access.ac-17-clone-private-include` | 2 / 4 |
| copy / `AC-17-clone-private-import` | `integration.module_multifile_access.ac-17-clone-private-import` | 4 / 4 |
| copy / `AC-17-clone-protected-include` | `integration.module_multifile_access.ac-17-clone-protected-include` | 2 / 4 |
| copy / `AC-17-clone-protected-import` | `integration.module_multifile_access.ac-17-clone-protected-import` | 4 / 4 |
| copy / `AC-18-clone-include` | `integration.module_multifile_access.ac-18-clone-include` | 2 / 4 |
| copy / `AC-18-clone-import` | `integration.module_multifile_access.ac-18-clone-import` | 4 / 4 |
| copy / `AC-19-clone-include` | `integration.module_multifile_access.ac-19-clone-include` | 2 / 4 |
| copy / `AC-19-clone-import` | `integration.module_multifile_access.ac-19-clone-import` | 4 / 4 |
| copy / `AC-20-clone-include` | `integration.module_multifile_access.ac-20-clone-include` | 2 / 4 |
| copy / `AC-20-clone-import` | `integration.module_multifile_access.ac-20-clone-import` | 4 / 4 |
| copy / `AC-14-flat-include` | `integration.module_multifile_access.ac-14-flat-include` | 2 / 4 |
| copy / `AC-14-flat-import` | `integration.module_multifile_access.ac-14-flat-import` | 4 / 4 |
| copy / `AC-15-flat-include` | `integration.module_multifile_access.ac-15-flat-include` | 2 / 4 |
| copy / `AC-15-flat-import` | `integration.module_multifile_access.ac-15-flat-import` | 4 / 4 |
| copy / `AC-16-flat-include` | `integration.module_multifile_access.ac-16-flat-include` | 2 / 4 |
| copy / `AC-16-flat-import` | `integration.module_multifile_access.ac-16-flat-import` | 4 / 4 |
| copy / `AC-17-flat-private-include` | `integration.module_multifile_access.ac-17-flat-private-include` | 2 / 4 |
| copy / `AC-17-flat-private-import` | `integration.module_multifile_access.ac-17-flat-private-import` | 4 / 4 |
| copy / `AC-17-flat-protected-include` | `integration.module_multifile_access.ac-17-flat-protected-include` | 2 / 4 |
| copy / `AC-17-flat-protected-import` | `integration.module_multifile_access.ac-17-flat-protected-import` | 4 / 4 |
| copy / `AC-18-flat-include` | `integration.module_multifile_access.ac-18-flat-include` | 2 / 4 |
| copy / `AC-18-flat-import` | `integration.module_multifile_access.ac-18-flat-import` | 4 / 4 |
| copy / `AC-19-flat-include` | `integration.module_multifile_access.ac-19-flat-include` | 2 / 4 |
| copy / `AC-19-flat-import` | `integration.module_multifile_access.ac-19-flat-import` | 4 / 4 |
| copy / `AC-20-flat-include` | `integration.module_multifile_access.ac-20-flat-include` | 2 / 4 |
| copy / `AC-20-flat-import` | `integration.module_multifile_access.ac-20-flat-import` | 4 / 4 |
| copy / `AC-21-include` | `integration.module_multifile_access.ac-21-include` | 2 / 4 |
| copy / `AC-21-import` | `integration.module_multifile_access.ac-21-import` | 4 / 4 |
| copy / `AC-22-include` | `integration.module_multifile_access.ac-22-include` | 2 / 4 |
| copy / `AC-22-import` | `integration.module_multifile_access.ac-22-import` | 4 / 4 |
| boundary / `ac23-observation-clone` | `integration.module_multifile_access.ac23-observation-clone` | 4 / 4 |
| boundary / `ac23-observation-flat` | `integration.module_multifile_access.ac23-observation-flat` | 4 / 4 |
| boundary / `ac23-snapshot-and-recursion` | `integration.module_multifile_access.ac23-snapshot-and-recursion` | 3 / 4 |
| boundary / `ac24-array` | `integration.module_multifile_access.ac24-array` | 3 / 4 |
| boundary / `ac25-26-factory-clone` | `integration.module_multifile_access.ac25-26-factory-clone` | 4 / 4 |
| boundary / `ac25-26-factory-flat` | `integration.module_multifile_access.ac25-26-factory-flat` | 4 / 4 |
| boundary / `ac27-protected` | `integration.module_multifile_access.ac27-protected` | 4 / 4 |
| boundary / `ac28-const-False` | `integration.module_multifile_access.ac28-const-false` | 3 / 4 |
| boundary / `ac28-const-True` | `integration.module_multifile_access.ac28-const-true` | 3 / 4 |
