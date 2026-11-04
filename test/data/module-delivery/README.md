# Autonomous module delivery migration (#388)

The C implementation is `test/src/functional/module_delivery_scenarios.c`.
The unchanged original `scripts/check_module_delivery.py` remains in this initial
parity stage. Its retirement is a separate change after the full original/C
platform matrix is green. No claim of completed CI parity is made here.

## Frozen inputs and independent oracles

The files in this directory come from the unchanged original at
`3c6f1d95b94a5343c6a1db2e26a20be1ced3b918`. `manifest.json` records its SHA-256,
the exact source/descriptor fixture hashes, and the two existing producer goldens:

- Portable main hash: `06ab143298bb8cd0a951a60efd33587c0ea89c17e438d3b2113043d0703b713b`.
- Bytecode SHA-256: `ba0a5dd1d608878e46e3d18c707bb2e3a1d21a53e6e62a93114485b051e83ccf`.

`leaf2.goat`, `leaf1.goat`, `middle.goat`, and `delivery.goat` are the exact
original source strings, extracted without executing the producer. The alternate
leaf uses the original `x*x` → `x+999` replacement. `expected.txt` contains the
original exact output including final newline and both signed 64-bit boundary
values. No expected output is regenerated from the production executable.

`descriptors/<name>-<version>-<mode>/module.gmod` preserves the original
`json.dumps` bytes, including spaces, field order, and the middle module's exact
`leaf: "2"` dependency. Alternate and foreign-runtime builds reuse the matching
required descriptor. Test data are read-only; sources are copied into each
scenario's own workspace and removed before consumer checks.

## Original → stable CTest map

All suffixes below are prefixed with `integration.module_delivery.`.
`end-to-end` runs the complete original sequence; independent scenario IDs select
the corresponding group with a fresh graph and environment.

| Original operation/check | CTest suffix | Contract retained |
| --- | --- | --- |
| Build leaf versions 2 and 1, middle `release`, delivery `opaque-release` | `producer`, `end-to-end` | Real compiler outputs, complete dependency graph, successful build with empty stdout, no initializer effects during compilation |
| Build required/off/auto delivery archives | `producer`, `end-to-end` | Equal main hash and bytecode; off has no native part; auto has native part; original cross-platform main/bytecode goldens |
| Inspect every produced archive | `producer`, `end-to-end` | Every ZIP entry deflated; independent portable hash and native-library SHA-256 checks |
| Read actual Goat PE/ELF machine | `producer`, `end-to-end` | Native target architecture/bits match executable rather than test-runner pointer width; PE i686/x86-64, little-endian ELF64 x86-64/AArch64 |
| Copy entire staging tree to delivery; copy required/off app; build replacement identity | `producer`, `dependencies`, `end-to-end` | Independent graph under `delivered/modules`, opaque versions, bytecode-only copy, distinct valid leaf2 for identity conflict |
| Merge required and off | `execution`, `end-to-end` | Exact merge stdout prefix/suffix, empty stderr, one native variant and valid deflated archive |
| Remove producers and all `.goat`/`.gmod` inputs | `execution`, `end-to-end` | No compiler/source inputs survive; consumer PATH points to empty directory, CC/CXX and explicit compiler are missing, module path cleared |
| Execute app off/auto/required, merged required, plain off | `execution`, `end-to-end` | Exact initializer order/result/boundary output, real native bound+succeeded counters, off succeeded=0 |
| Plain required failure | `execution`, `end-to-end` | Nonzero status with diagnostic and empty stdout before initializer effects |
| Delivered archive immutability after execution | `execution`, `end-to-end` | Every pre-existing `.gbin` remains byte-identical |
| Inspect app and absent path | `execution`, `end-to-end` | Published JSON schema, report version/success, error-code presence, empty stderr, missing-file failure |
| Unknown native ABI 2 | `unknown-abi`, `end-to-end` | Same portable identity; off/auto execute; required fails before initialization; every own variant incompatible and not checked with `NO_COMPATIBLE_VARIANT` |
| Windows MINGW64 and UCRT64 producer compiler probes | `windows-runtimes`, `end-to-end` | Mandatory absolute real compiler files, x86-64 dumpmachine/macros, compiler's own bin first on PATH, real DLLs and exact runtime target families |
| Mixed/foreign Windows merges | `windows-runtimes`, `end-to-end` | MINGW32 gets three mixed/two foreign variants; 64-bit gets two mixed/one foreign, both CRT families; avoid rebuilding the own target twice |
| Fake MSYSTEM MINGW32/MINGW64/UCRT64 × off/auto/required | `windows-runtimes`, `end-to-end` | Nine actual executions, environment cannot select an incompatible native target |
| Mixed inspection and foreign-only execution | `windows-runtimes`, `end-to-end` | Exactly one compatible checked/passed variant matches executable architecture/runtime; others unprobed; foreign off/auto succeed, required fails; foreign variants incompatible/unprobed |
| Delete exact leaf2 dependency | `dependencies`, `end-to-end` | All three run modes fail before initialization, no highest-version fallback, inspect fails with identical English/Russian error code sets |
| Replace leaf2 with alternate valid identity; restore original | `dependencies`, `end-to-end` | All modes reject recorded-hash mismatch; inspect fails; restored graph executes required successfully |
| Append `changed native payload` | `integrity-native-hash`, `end-to-end` | All three modes fail before initializer effects; English/Russian inspector error code sets agree |
| Append `changed portable payload` | `integrity-bytecode-hash`, `end-to-end` | Same negative guarantees, including native-off validation |
| Replace module JSON with `{bad json` | `integrity-json`, `end-to-end` | Same negative guarantees without accepting a parser crash as an ordinary error |
| Repackage stored and alternating stored/deflated entries | `compression-stored`, `compression-mixed`, `end-to-end` | Exact decompressed metadata/bytecode/native bytes and main hash retained; required execution and inspection pass |
| Deliberately wrong observation/input controls | `controls` | Always-active Release validation rejects corruption rather than silently accepting a bad result |

The original explicitly delegates shared-identity diamond conflicts and all
conflicting provenance paths to `integration.module_graph`; this migration does
not remove or duplicate that separate coverage. Windows-only runtime branches
have an explicit not-applicable result on non-Windows hosts, never on a missing
Windows compiler or failed DLL probe. The ordinary delivery graph remains
applicable on Linux x86-64/AArch64 and all existing MinGW environments, plus the
existing musl and package contexts.

## Direct reproduction

```sh
cmake -S . -B build/delivery -DCMAKE_BUILD_TYPE=Release
cmake --build build/delivery --target module_delivery_scenarios
ctest --test-dir build/delivery --output-on-failure \
  -R '^integration.module_delivery\.'
build/delivery/module_delivery_scenarios \
  integration.module_delivery.end-to-end \
  build/delivery/goat build/delivery/goat-inspect build/delivery/goat-merge \
  /usr/bin/cc build/delivery test/data docs/module-format-v1 --keep-work
```

The CLI is `ID|--list GOAT INSPECT MERGE CC BUILD DATA FORMAT_ROOT [--keep-work]`.
Use the selected MinGW compiler and executable paths on Windows. The runner owns
an isolated `scenario-*` directory under BUILD and a shared scenario deadline.
Shared process/filesystem helpers retain diagnostics on failure; `--keep-work`
also retains success. Consumer artifacts are under `delivery work/delivered`.
Checks remain active in Release. A cleanup error is a failure, even when the
child itself exits zero. In particular, local sandbox absence of
`/proc/thread-self/children` is not permission to bypass the existing cleanup
contract or to claim the complete local run passed.

## Temporary same-input parity gate

```sh
python3 test/support/module_delivery_parity.py \
  build/delivery/goat build/delivery/goat-inspect build/delivery/goat-merge \
  /usr/bin/cc build/delivery/module_delivery_scenarios \
  build/delivery test/data docs/module-format-v1 --phase-timeout 600
```

The wrapper first checks the original script and every frozen input against
`manifest.json`. It runs the unchanged original via `runpy` in a separate Python
process, preserving every assertion and actual subprocess invocation. Two hooks
retain the temporary directory and verify each original `put` source/descriptor
against frozen inputs. They do not alter source, outputs, exit statuses, schema
checks, native dispatch, compiler commands, or expected values. Exact expected
source write counts also cover the additional Windows producers.

The wrapper then executes C `end-to-end` against the same real tool paths and
compiler, retaining its separate workspace. Both complete runners must pass.
The comparison checks the complete final delivered `.gbin` tree, member names and
compression methods, all portable bytes, full metadata except native library
hashes, and the native hash-validation/corrupt-suffix state. Actual native bytes
and their hashes may differ between independent builds because the temporary
source paths and PE timestamps differ; no target/ABI/provenance field is dropped.
Original command counts (49 on Linux, 71 on Windows) must match the complete
sequential C execution log, including inspection commands. The multiset of
complete dispatch-counter records must also match, so repeated
Windows fake-environment executions and mode/fallback behavior are observable.
Neither workspace may retain `.goat` or `.gmod` files.

Artifacts and both phase stdout/stderr logs remain under
`BUILD/scenario-module-delivery-parity-*`. Each phase has its own finite positive
`--phase-timeout` (default 600 seconds). The original keeps its existing Python
subprocess deadlines; the C runner uses the shared owned-process facilities.
The temporary `module_delivery_parity_host` target wraps each complete phase
using the same shared C supervision and deadline facilities, retaining its
diagnostics. The wrapper never uses a Python timeout to kill the supervising
host before owned process-tree cleanup finishes. No separate Python tree-cleanup
implementation or relaxed cleanup check is introduced.
Package parity uses 300 seconds per phase; its wrapper allowance is 660 seconds.
CTest parity uses 600 per phase and a 1,260-second aggregate allowance. The extra
60 seconds is for comparison and diagnostic/cleanup overhead, not a replacement
for either phase's deadline. The exact initial parity and later retirement CI
links must be recorded before marking checklist item #388 complete.
