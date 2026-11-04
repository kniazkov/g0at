# Autonomous module delivery migration (#388)

The C implementation is `test/src/functional/module_delivery_scenarios.c`.
The original Python runner and temporary parity wrapper/host were retired only
after complete original/C parity on `67edfd3582ca8d5f4aa65ab05fff2304e6ca7ea6`:
[build/test 11/11](https://github.com/kniazkov/g0at/actions/runs/41766231298) and
[format vectors 3/3](https://github.com/kniazkov/g0at/actions/runs/41766231321),
tracked in [PR #469](https://github.com/kniazkov/g0at/pull/469).
The final C-only CI is checked separately.

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

The CLI is `ID|--list GOAT INSPECT MERGE CC BUILD DATA FORMAT_ROOT [--keep-work] [--timeout-seconds SECONDS]`.
Use the selected MinGW compiler and executable paths on Windows. The runner owns
an isolated `scenario-*` directory under BUILD and a shared scenario deadline.
Shared process/filesystem helpers retain diagnostics on failure; `--keep-work`
also retains success. Consumer artifacts are under `delivery work/delivered`.
Checks remain active in Release. A cleanup error is a failure, even when the
child itself exits zero. In particular, local sandbox absence of
`/proc/thread-self/children` is not permission to bypass the existing cleanup
contract or to claim the complete local run passed.

## Completed same-input parity gate

The retired wrapper ran the unchanged original and C end-to-end against the same
frozen inputs and real tool paths in independent workspaces. It compared complete
archive graphs, portable bytes, metadata, compression methods, native validation
state, all dispatch counters and exact command counts (49 Linux, 71 Windows).
The linked green gate covered Debug/Release, musl and relocated release packages.
The provenance manifest retains the original script path and digest as historical
evidence; normal C runs do not read the removed script or regenerate fixtures.

The 12 CTest IDs above remain. The former aggregate Python ID
`integration.module_delivery` and temporary `.parity` ID are removed; use
`integration.module_delivery.end-to-end` for the complete C suite.

Normal scenarios use a 570-second owned deadline within CTest's 600-second limit.
The package caller selects `--timeout-seconds 300`, retaining its original
300-second outer budget and allowing owned cleanup before it expires. Each child
keeps the original 180-second limit, bounded by the remaining scenario budget.
The optional total budget accepts whole seconds from 31 through 600, reserving
30 seconds for cleanup before the outer deadline; it cannot extend
the ordinary budget. `--keep-work` remains available with either budget.
