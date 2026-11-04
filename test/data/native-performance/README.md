# Native performance migration (#387)

The C runner is `test/src/functional/native_performance_scenarios.c`.
The original Python checker and temporary parity wrapper were retired only after
complete original/C CI parity on `37ce62bb55f0a9f68c35521d8349de5c9a892f5d`:
[build/test matrix, 11/11](https://github.com/kniazkov/g0at/actions/runs/41742078675)
and [module vectors, 3/3](https://github.com/kniazkov/g0at/actions/runs/41742078588),
under [PR #468](https://github.com/kniazkov/g0at/pull/468).
The separate final-retirement commit's CI is pending; the parity result does not
claim that later commit has already passed.

Performance remains a separate serial Release run, not part of ordinary
correctness or sanitizer selection. No language behavior, `.gbin` format,
production arithmetic or expected result is changed.

## Original-to-C scenario map

Stable CTest IDs have the prefix `integration.native_performance.`.

| Original operation/check | CTest suffix | Preserved contract |
| --- | --- | --- |
| `program()` and `expected_result()` | `corpus`, `benchmark` | Depth 12, 1,024 trees, 4,095 calls per tree, 4,193,280 calls total; independent result 771246522368 |
| Compile `vm.goat` and `native.goat` | `benchmark` | `--lang en --compile --native off|required`; successful exit and empty stdout/stderr; no compilation in samples |
| Delete original sources, prohibit compiler | `benchmark` | Both `.goat` inputs removed before execution; `CC=goat-no-such-compiler` in the execution environment |
| Persist `call_tree.goat` for diagnostics | `benchmark`, `corpus` | Same generated source retained separately; it is not an execution input |
| VM and native warmups | `benchmark` | One complete checked run of each mode outside the five measured samples |
| Five measured pairs | `benchmark` | VM/native, native/VM, VM/native, native/VM, VM/native; no parallel benchmark runs |
| `execute()` | `benchmark`, `control-output` | Each child has a 180-second cap; zero exit, empty stderr; stdout normalizes CRLF only |
| Exact result comparison | `benchmark`, `control-output` | Exactly `771246522368\n`, not a substring or trimmed answer |
| Dispatch report parsing | `benchmark`, `control-report` | VM preparation `disabled`, attempts/succeeded `0`; native preparation `ready`, attempts/succeeded `1024`; retries `0` in both modes |
| Median and strict speed comparison | `benchmark`, `control-timings` | Median of each five samples; `native < vm`, never equality; neither compilation nor warmup contributes |
| `timings.json` | `benchmark`, `control-timings` | Original keys, invariant integers, two five-element nanosecond arrays, medians, speedup and boolean passed |
| OS-dependent source newlines and clock | `corpus`, `benchmark` | Linux and Windows source-file semantics and monotonic elapsed timing; stdout CRLF normalization does not trim other bytes |

The extra `control-*` scenarios deliberately supply incorrect observations to
the same validators used by the benchmark. They are correctness tests rather
than additional performance samples. Checks use always-active scenario
requirements, not assertions disabled by `NDEBUG` in Release.

## Platforms and retained diagnostics

The original benchmark is applicable to Linux x86-64/AArch64 and Windows
MinGW i686/x86-64/UCRT native-capable Release CI jobs. None of these branches is
silently converted to SKIP. The historical standalone performance invocation
does not run in musl or sanitizer jobs; this migration does not introduce a new
performance gate there. Ordinary controls can run independently of performance.
A Debug benchmark is not a substitute for the Release timing gate.

Each C scenario owns a unique `scenario-*` directory below the supplied build
base and treats `test/data` as read-only. Shared scenario execution supplies the
outer deadline, per-command deadline scopes, owned process-tree cleanup and
binary stdout/stderr diagnostics. Failed work is retained automatically;
`--keep-work` retains successful work. The benchmark retains `timings.json`,
`call_tree.goat`, compiled inputs and warmup/sample dispatch reports when kept.
A failed compile, timeout, cleanup error, missing report, wrong result or failed
speed comparison must remain a failure.

In particular, a local sandbox missing `/proc/thread-self/children` can reproduce
an existing child-cleanup `ENOENT`. That is a local validation blocker, not
permission to suppress cleanup checks, weaken supervision or claim full parity.
CI must establish the complete original/C result on its actual supported hosts.

## Direct reproduction

Configure and build the Release target, then select the standalone CTest:

```sh
CC=gcc cmake -S . -B build/native-performance -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-performance --target native_performance_scenarios
ctest --test-dir build/native-performance --output-on-failure \
  -R '^integration.native_performance.benchmark$'
```

CTest passes `--keep-work` and uses the existing spaced build directory
`build/native-performance/ctest/native performance`; its unique `scenario-*`
child contains `timings.json`, sources for diagnosis, binaries and dispatch
reports. Non-Release CTest configurations explicitly disable only the benchmark.
For direct CLI execution, use an existing build directory:

```sh
CC=gcc build/native-performance/native_performance_scenarios \
  integration.native_performance.benchmark \
  build/native-performance/goat \
  'build/native-performance/ctest/native performance' test/data --keep-work
```

The CLI is `ID|--list GOAT BUILD DATA [--keep-work]`. Substitute `corpus`,
`control-output`, `control-report`, or `control-timings` for the suffix to run a
single independent correctness scenario. On Windows use the `.exe` paths and
the shell's normal path conventions. Keep the benchmark serial and separate
from correctness tests so their load does not contaminate measurements.

## Completed original/C parity

The historical [original](https://github.com/kniazkov/g0at/blob/37ce62bb55f0a9f68c35521d8349de5c9a892f5d/scripts/check_native_performance.py)
and [parity wrapper](https://github.com/kniazkov/g0at/blob/37ce62bb55f0a9f68c35521d8349de5c9a892f5d/scripts/check_native_performance_parity.py)
remain reproducible from the exact green parity commit linked above. They are
not required or invoked by the current runner, CTest or CI.

The full gate ran the unchanged original and complete C benchmark separately
and retained their logs and workspaces. It checked:

- Identical generated `call_tree.goat` bytes after CRLF-to-LF normalization.
- Identical depth, tree/call counts and independently calculated expected result.
- Exactly five positive integer nanosecond samples for each mode, with medians
  recomputed independently from those arrays.
- A finite speedup consistent with the medians and a true, consistent `passed`.
- No original `vm.goat` or `native.goat`, and both compiled `.gbin` files present.
- Matching required dispatch fields in all twelve warmup/sample reports.

Elapsed values were not required to match across executions. Each benchmark
independently required native to be faster than VM; the wrapper compared inputs,
methodology and report invariants, never reinterpreting a failed runner as a pass.

## Timing-support dependency and its controls

`test/support/execution.{h,c}` adds `scenario_command_capture_timed`; its
nanosecond clock is `test/support/monotonic.{h,c}` (Linux `CLOCK_MONOTONIC`, Windows
`QueryPerformanceCounter`, checked quotient/remainder conversion). Only the new
API selects the existing owned-child bounded memory capture. The interval starts
immediately before child creation and ends immediately after the owned wait.
Command diagnostics, artifact persistence/rereads, output/report validation,
source generation, compilation and warmup samples are outside measured samples.
Both streams are limited to `CHILD_OUTPUT_LIMIT` (65,536 bytes each); truncation
fails explicitly after saving the captured diagnostic prefix. Ordinary unbounded
scenario capture is unchanged. No production process implementation is changed.

Stable support IDs are `framework.timed-execution.` followed by `clock`,
`success`, `nonzero`, `boundary`, `duration`, `truncate-out`, `truncate-err`,
`timeout`, `crash`, `start-error`, `null-clock`, `cleanup-failure`,
`descendant-exit`, or `descendant-timeout`. They test the real timed API with
binary dual-stream output, an exact capacity boundary and overflow, rejected
execution failures, retained partial diagnostics, cleanup and an independent
process-identity observer. They run as correctness checks in Debug/Release,
including the existing musl correctness selections. The benchmark alone has
`performance` labels and serial registration; it has a 2,520-second shared
budget (14 commands at up to 180 seconds) and a 2,550-second CTest cleanup margin.
CI and direct CTest benchmark paths retain spaces in directory names.
The report speedup is rounded explicitly to binary64 before exact JSON validation
on x87; an adjacent-binary64 corruption is rejected. `timings-candidate.json`
is persisted before validation. Windows descendant tests wait boundedly on the
pinned process handle before checking termination and removing the workspace;
no observer kill or production cleanup change is used.

The frozen `call_tree.goat` was extracted by invoking the unchanged original
`program()` at base `e8950f2cc96e8990b05981c9c2d7816ad288f32a`. Its LF form is
30,417 bytes, SHA-256
`f6813be018340056338326b6a891a8dbe96535e0bd2359cb5e3e81244c6f40d2`.
The C generator is independently implemented and compared byte-for-byte with
CRLF normalization; corpus controls reject altered bytes and truncation.
