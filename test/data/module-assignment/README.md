# Assignment access and evaluation-order fixtures (#361)

Copyright 2026 Ivan Kniazkov. Distributed under the repository MIT license.

`scenarios.json` preserves the concrete expansion of
`scripts/check_module_assignment_access.py` from
`89fffac30bb96b067eba3316292ed1b464896d90`: **42 host projects / 90 compile-VM
operations**, plus **13 CLI cases with each of two optimizers / 26 operations**.
There are **68 independently selectable CTest IDs**, with a total of 116 checked
commands, exactly as in the original runner. No random generation is involved.

The implementation is
[`module_assignment_scenarios.c`](../../src/functional/module_assignment_scenarios.c).
Source strings and expected observations are stored only in this read-only table;
CMake derives IDs from the same table. The runner uses the existing
`test/support/module_execution` helpers and common scenario framework. The old
Python runner had no other importers and is removed after parity; the shared
Python execution helper remains for its other consumers until #394.

## Preserved coverage

- Rule 13: dot/index share field rights; hidden-field errors stop RHS effects.
- Rules 14–16: object/key evaluate once, rights/constant/foreign-extension checks
  precede RHS, and imported public/external fields preserve access and constness.
- Rules 17–18: compound assignment reads once; throwing RHS preserves only its
  own writes and effects, without a final store.
- Rules 19–20: destination object/key are captured before RHS rebinding;
  object/key exceptions and key-driven mutation preserve exact evaluation order.
- Rules 21–23: final write rechecks newly private fields, no placeholder is
  inserted before RHS, and an RHS-created field keeps its original provenance.
- Rules 24–25: arrays retain a position, not an element identity; removing or
  clearing changes the final target and error without undoing RHS effects.
- CLI constants, capture, key mutation, RHS exceptions, both array mutations and
  chained assignment run under `--optimize none` and `all`. As in Python, native
  selection and environment are inherited defaults, not forced to `off` and not
  replaced with a project `GOAT_MODULE_PATH`.
- Host programs retain real includes/imports and persisted binary execution;
  normal zero exit and exact stdout (CRLF-normalized only) are required.
  Caught `ACCESS_DENIED`, `PROPERTY_IS_CONSTANT`, `FOREIGN_OBJECT`,
  `INVALID_ARGUMENT` and user exceptions remain part of expected stdout.

Each scenario owns a unique build workspace with a common 120-second deadline
and 150-second CTest cleanup margin. Processes are supervised, full commands and
streams are retained on failure, success removes work unless `--keep-work` is
selected. Source data is read-only. No platform-specific SKIP cases exist;
Linux x86-64/AArch64 GCC/Clang, Windows MinGW32/MinGW64/UCRT64 and musl run the
same table in the existing Debug/Release CI paths.

## Reproduction and verification

```sh
cmake -S . -B build/assignment-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE
cmake --build build/assignment-debug --parallel 4 --target module_assignment_scenarios
ctest --test-dir build/assignment-debug --output-on-failure --parallel 4 -L module-assignment-migration
# Repeat with another build directory and -DCMAKE_BUILD_TYPE=Release.
build/assignment-debug/module_assignment_scenarios integration.module_assignment_access.cli-chain-all build/assignment-debug/module_import_execution_testing build/assignment-debug/goat build/assignment-debug test/data test/data --keep-work
# Replace the ID by --list to list every scenario.
```

The last positional argument is a fixture root, normally `test/data`; copied
negative fixtures can be selected without modifying checked-in input data.

Before Python deletion:

- Debug, Release (both with Python discovery disabled) and UBSan
  (`-fsanitize=undefined -fno-sanitize-recover=all`): 68/68 CTest IDs each.
- Original Python Debug/Release: all 116 checks pass on the same host/CLI
  binaries. Captured **executed** compile arguments (including every source and
  included file), ordered operations, CLI modes and expected stdout exactly
  match the checked-in table. C run logs independently total 90 host + 26 CLI.
- Negative Release self-checks reject wrong host stdout, a deliberately reversed
  CLI `okr` effect order, malformed JSON and an invalid operation mode. Runtime
  failures retain diagnostics; `--keep-work` retains successful output.
- Source table SHA-256 unchanged by all runs; parallel execution and cleanup
  verified. Related access/provenance regression: 166/166.

The original runner can be reproduced with:

```sh
mkdir -p build/assignment-python
git show 89fffac30bb96b067eba3316292ed1b464896d90:scripts/check_module_assignment_access.py > build/assignment-python/check.py
PYTHONPATH="$PWD/scripts" python3 build/assignment-python/check.py build/assignment-debug/module_import_execution_testing build/assignment-debug/goat
```

## Exact legacy → CTest map

Legacy host names contain their original suite-counter suffix; these are now
fixed IDs, independent of execution order. CLI operator names use `assign` for
`=` and `add` for `+=`; each optimizer is independently selectable. Related
assertions stay together inside each original project/CLI case.

| Original project or CLI case | New CTest ID | Checked commands |
|---|---|---:|
| `13-shared-0` | `integration.module_assignment_access.13-shared-0` | 2 |
| `13-shared-2` | `integration.module_assignment_access.13-shared-2` | 2 |
| `13-hidden-4` | `integration.module_assignment_access.13-hidden-4` | 2 |
| `13-hidden-6` | `integration.module_assignment_access.13-hidden-6` | 2 |
| `14-early-8` | `integration.module_assignment_access.14-early-8` | 2 |
| `14-early-10` | `integration.module_assignment_access.14-early-10` | 2 |
| `15-early-12` | `integration.module_assignment_access.15-early-12` | 2 |
| `15-early-14` | `integration.module_assignment_access.15-early-14` | 2 |
| `15-early-16` | `integration.module_assignment_access.15-early-16` | 2 |
| `15-early-18` | `integration.module_assignment_access.15-early-18` | 2 |
| `16-early-20` | `integration.module_assignment_access.16-early-20` | 2 |
| `16-early-22` | `integration.module_assignment_access.16-early-22` | 2 |
| `15-foreign-extension-24` | `integration.module_assignment_access.15-foreign-extension-24` | 3 |
| `15-foreign-extension-27` | `integration.module_assignment_access.15-foreign-extension-27` | 3 |
| `foreign-fields-30` | `integration.module_assignment_access.foreign-fields-30` | 3 |
| `foreign-fields-33` | `integration.module_assignment_access.foreign-fields-33` | 3 |
| `foreign-fields-36` | `integration.module_assignment_access.foreign-fields-36` | 3 |
| `foreign-fields-39` | `integration.module_assignment_access.foreign-fields-39` | 3 |
| `17-value-42` | `integration.module_assignment_access.17-value-42` | 2 |
| `18-value-44` | `integration.module_assignment_access.18-value-44` | 2 |
| `17-value-46` | `integration.module_assignment_access.17-value-46` | 2 |
| `18-value-48` | `integration.module_assignment_access.18-value-48` | 2 |
| `19-20-captured-50` | `integration.module_assignment_access.19-20-captured-50` | 2 |
| `19-20-captured-52` | `integration.module_assignment_access.19-20-captured-52` | 2 |
| `19-20-captured-54` | `integration.module_assignment_access.19-20-captured-54` | 2 |
| `19-20-captured-56` | `integration.module_assignment_access.19-20-captured-56` | 2 |
| `left-throws-58` | `integration.module_assignment_access.left-throws-58` | 2 |
| `left-throws-60` | `integration.module_assignment_access.left-throws-60` | 2 |
| `left-throws-62` | `integration.module_assignment_access.left-throws-62` | 2 |
| `left-throws-64` | `integration.module_assignment_access.left-throws-64` | 2 |
| `key-mutates-target-66` | `integration.module_assignment_access.key-mutates-target-66` | 2 |
| `key-mutates-target-68` | `integration.module_assignment_access.key-mutates-target-68` | 2 |
| `21-final-private-70` | `integration.module_assignment_access.21-final-private-70` | 2 |
| `21-final-private-72` | `integration.module_assignment_access.21-final-private-72` | 2 |
| `22-no-placeholder-74` | `integration.module_assignment_access.22-no-placeholder-74` | 2 |
| `22-no-placeholder-76` | `integration.module_assignment_access.22-no-placeholder-76` | 2 |
| `23-update-created-78` | `integration.module_assignment_access.23-update-created-78` | 2 |
| `23-update-created-80` | `integration.module_assignment_access.23-update-created-80` | 2 |
| `24-array-82` | `integration.module_assignment_access.24-array-82` | 2 |
| `25-array-84` | `integration.module_assignment_access.25-array-84` | 2 |
| `24-array-86` | `integration.module_assignment_access.24-array-86` | 2 |
| `25-array-88` | `integration.module_assignment_access.25-array-88` | 2 |
| `constant-= / none` | `integration.module_assignment_access.cli-constant-assign-none` | 1 |
| `capture-= / none` | `integration.module_assignment_access.cli-capture-assign-none` | 1 |
| `key-mutation-= / none` | `integration.module_assignment_access.cli-key-mutation-assign-none` | 1 |
| `rhs-throws-= / none` | `integration.module_assignment_access.cli-rhs-throws-assign-none` | 1 |
| `array-=-False / none` | `integration.module_assignment_access.cli-array-assign-false-none` | 1 |
| `array-=-True / none` | `integration.module_assignment_access.cli-array-assign-true-none` | 1 |
| `constant-+= / none` | `integration.module_assignment_access.cli-constant-add-none` | 1 |
| `capture-+= / none` | `integration.module_assignment_access.cli-capture-add-none` | 1 |
| `key-mutation-+= / none` | `integration.module_assignment_access.cli-key-mutation-add-none` | 1 |
| `rhs-throws-+= / none` | `integration.module_assignment_access.cli-rhs-throws-add-none` | 1 |
| `array-+=-False / none` | `integration.module_assignment_access.cli-array-add-false-none` | 1 |
| `array-+=-True / none` | `integration.module_assignment_access.cli-array-add-true-none` | 1 |
| `chain / none` | `integration.module_assignment_access.cli-chain-none` | 1 |
| `constant-= / all` | `integration.module_assignment_access.cli-constant-assign-all` | 1 |
| `capture-= / all` | `integration.module_assignment_access.cli-capture-assign-all` | 1 |
| `key-mutation-= / all` | `integration.module_assignment_access.cli-key-mutation-assign-all` | 1 |
| `rhs-throws-= / all` | `integration.module_assignment_access.cli-rhs-throws-assign-all` | 1 |
| `array-=-False / all` | `integration.module_assignment_access.cli-array-assign-false-all` | 1 |
| `array-=-True / all` | `integration.module_assignment_access.cli-array-assign-true-all` | 1 |
| `constant-+= / all` | `integration.module_assignment_access.cli-constant-add-all` | 1 |
| `capture-+= / all` | `integration.module_assignment_access.cli-capture-add-all` | 1 |
| `key-mutation-+= / all` | `integration.module_assignment_access.cli-key-mutation-add-all` | 1 |
| `rhs-throws-+= / all` | `integration.module_assignment_access.cli-rhs-throws-add-all` | 1 |
| `array-+=-False / all` | `integration.module_assignment_access.cli-array-add-false-all` | 1 |
| `array-+=-True / all` | `integration.module_assignment_access.cli-array-add-true-all` | 1 |
| `chain / all` | `integration.module_assignment_access.cli-chain-all` | 1 |
