# Native performance migration (#387)

The original `scripts/check_native_performance.py` remains unchanged during the
original/C parity gate. Its C replacement is
`test/src/functional/native_performance_scenarios.c`. Performance is a separate
Release run, not part of the ordinary correctness or sanitizer test selection.
No language behavior, `.gbin` format, production arithmetic, or expected result
is changed by this migration. Checklist step 69 remains pending until full
original/C CI parity, retirement of the original, and final CI are verified.

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

## Direct reproduction and temporary parity

Configure and build the Release target, then use the stable scenario ID:

```sh
CC=gcc cmake -S . -B build/native-performance -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-performance --target native_performance_scenarios
CC=gcc build/native-performance/native_performance_scenarios \
  integration.native_performance.benchmark \
  build/native-performance/goat build/native-performance test/data --keep-work
```

The CLI is `ID|--list GOAT BUILD DATA [--keep-work]`. Substitute `corpus`,
`control-output`, `control-report`, or `control-timings` for the suffix to run a
single independent correctness scenario. On Windows use the `.exe` paths and
the shell's normal path conventions. Keep the benchmark serial and separate
from correctness tests so their load does not contaminate measurements.

Until the original/C gate is complete, run the temporary wrapper:

```sh
CC=gcc python3 scripts/check_native_performance_parity.py \
  build/native-performance/goat \
  build/native-performance/native_performance_scenarios \
  build/native-performance test/data
```

It first runs the unchanged original Python checker, then the complete C
benchmark, each with its own workspace below a unique
`native-performance-parity-*` root. Both must exit successfully. Both stdout
and stderr logs and all generated work are retained. The wrapper requires:

- Identical generated `call_tree.goat` bytes after CRLF-to-LF normalization.
- Identical depth, tree/call counts and independently calculated expected result.
- Exactly five positive integer nanosecond samples for each mode, with medians
  recomputed independently from those arrays.
- A finite speedup consistent with the medians and a true, consistent `passed`.
- No original `vm.goat` or `native.goat`, and both compiled `.gbin` files present.
- Matching required dispatch fields in all twelve warmup/sample reports.

Elapsed values must **not** be numerically equal across executions. The
benchmark compares native against VM within each run; the parity wrapper
compares methodology, inputs and report invariants across runs. It does not
invent tolerance for a failed speed comparison or reinterpret a failed runner
as successful. The original and wrapper are removed only after the complete
CI parity gate, followed by separate CI for the retirement change. Green CI
links belong here and in the issue/checklist once those exact commits pass.

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
The temporary `performance.native` original/C parity gate has a 5,100-second
outer limit. CI and direct CTest benchmark paths retain spaces in directory names.

The frozen `call_tree.goat` was extracted by invoking the unchanged original
`program()` at base `e8950f2cc96e8990b05981c9c2d7816ad288f32a`. Its LF form is
30,417 bytes, SHA-256
`f6813be018340056338326b6a891a8dbe96535e0bd2359cb5e3e81244c6f40d2`.
The C generator is independently implemented and compared byte-for-byte with
CRLF normalization; corpus controls reject altered bytes and truncation.
