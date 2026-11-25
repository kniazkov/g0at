# GC final audit and CI gates (#643)

Automatic allocation-triggered collection is enabled in CLI/REPL, including
module/native execution. Embedding opts in under the idle process model lock.
See [policy](gc-allocation-policy.md), [boundary contract](gc-boundary-audit.md),
[pressure](gc-memory-pressure.md) and [fatal OOM](gc-fatal-oom.md).
Forced instruction-boundary collection is a separate test mode, disabled by default.

## Reproducible, bounded acceptance

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4 --target gc_acceptance_runners
cmake -DBUILD_DIR=build -P scripts/check_gc_acceptance.cmake
```

The runner verifies the checked-in `scripts/gc-acceptance-tests.txt` manifest
against CTest's actual inventory. Every required test must retain `gc-acceptance`,
a positive timeout of at most 180 seconds and exactly one execution group.
Removing a registration or its label fails before execution. Additions are allowed;
intentional removal requires review of both the manifest and this audit.

There are 328 mandatory registrations: 325 `gc-regression` tests and three
`gc-stress` tests. Groups run serially with total deadlines of 900 and 600 seconds,
respectively; there are no automatic retries. The CI acceptance step allows
27 minutes including runner build. A test timeout remains a failure, not a skip.
The script stores `gc-regression.log` and `gc-stress.log` in the build directory;
CI uploads them, including successful stress measurements. Tests depending on an
archive retain their CTest setup fixture.

| Required CI path | Configuration |
|---|---|
| Linux x86_64/aarch64, GCC/Clang | Debug and Release, both groups |
| Windows MINGW32/MINGW64/UCRT64 | Debug and Release, both groups |
| Linux x86_64, GCC and Clang | Separate full-runtime ASan/UBSan builds, both groups, 30-minute job limit |
| Allocation failures | Existing separate fault builds, `gc-oom` tests; 19 Linux / 18 Windows |

The sanitizer build instruments the runtime and runners, not only generated code.
Undefined behavior is fatal. LSan is explicitly disabled; this does not claim a
successful leak-sanitizer run. Fixtures independently check allocator balances.
Fatal OOM intentionally terminates without teardown and is tested in subprocesses,
not interpreted as an ordinary sanitizer leak.

## Original defects: permanent evidence

The following paths identify permanent fixtures in the repository. The first two
startup-root cases now have their own acceptance registration; previously they
were only in the general unit list.

| Original #542 finding | Permanent regression / source | Result or limit |
|---|---|---|
| Cycles accumulate until run end (#553) | `core.gc_policy.*`, `core.gc_cli.*`; `test/src/component/gc_policy_testing.c`, `gc_cli_testing.c` | In-run progress, production launchers, automatic stress |
| Sweep retains refs to live neighbours (#544) | `core.gc_sweep`; `test/src/unit/test_model.c` | Arrays, fields and function edges reconciled |
| Recursive marking (#545) | `core.gc_mark`; `test_gc_iterative_mark` in `test_model.c` | Deep graph on bounded OS-thread stack |
| Recursive last DECREF (#546) | `core.gc_release`; `test_iterative_last_ref_release` in `test_model.c` | Deep destruction on bounded stack |
| Missing startup callable root (#543) | `core.gc_startup_roots`; `test_gc_pending_thread_callable`, `test_gc_no_registered_threads` in `test_model.c` | Closure callable after collection; empty thread registry |
| Unsafe/non-nestable external roots (#547) | `core.gc_roots`; `test_owning_gc_roots` in `test_model.c` | Independent owning registrations and release |
| Missing in-run safety protocol (#552) | `core.gc_protocol`, `core.gc_dispatch.*`, `core.gc_runtime*`, `core.gc_module_initialization.*`, `core.gc_embedding` | Reviewed publication only; boundary matrix lists exclusions |
| Completed threads retain payload (#549) | `core.thread_memory.*`; `test/src/component/module_thread_lifetime_testing.c` | join/no-join, unobserved results, queued startup |
| Pools retain unbounded backing storage (#550) | `core.gc_pools`, `core.gc_policy.pressure*` | Aggregate byte budget and explicit trim, no RSS guarantee |
| Synchronous collection pauses (#551) | `core.gc_stats`, `core.gc_policy.stress-*` | Phase pause measurements; no incremental or real-time guarantee |
| OOM exits immediately (#554) | `fault.gc_oom.*`; `test/src/component/gc_oom_testing.c` | Accepted fatal contract, bounded diagnostic path; no retry/recovery |
| Inconsistent/overflowing refcounts (#548) | `core.gc_refcounts`; `test/src/unit/test_model.c` | Checked arithmetic; overflow rejected |
| Insufficient observation/combined tests (#555) | `core.gc_stats`, `core.gc_acceptance.*`, stress manifest | In-run bounds, repeated runs, allocator balance, raw measurements |
| Cross-process reference ownership (#556) | `core.gc_ownership`; `test/src/unit/test_gc_ownership.c` | Explicit ownership checks, no implicit transfer permission |

These fixtures are also reachable through the source tree at
[the accepted stress baseline](https://github.com/kniazkov/g0at/tree/099411c9/test/src).
The new startup-root selector changes coverage selection, not those regression bodies.

## Evidence, limitations and closure

PR #656's final commit `6e96c268` passed all 14 checks: 11 main jobs and three
module-format jobs. Its initial Doxygen link failure was fixed; a subsequent
GitHub startup failure created no jobs and was **not** counted as acceptance.
The successful rerun is
[run 45649602762](https://github.com/kniazkov/g0at/actions/runs/45649602762).
That evidence predates the new sanitizer jobs and manifest; this PR requires its
own complete CI. Historical per-stage measurements remain in the issue archive.

Arbitrary nonreturning C callbacks can postpone GC indefinitely. Accounting is
logical owned payload, not RSS or all malloc traffic. The collector remains a
full synchronous pass; fixed stress budgets are not application latency SLAs.
Embedding callers must honor idle configuration, model-lock and ownership rules.

[#645](https://github.com/kniazkov/g0at/issues/645) remains an unresolved
infrastructure blocker for closing the overall campaign: a later ELF probe
reported `CHILD_IO_ERROR`, `cleanup=EIO` while receiving the supervisor reply.
The earlier generic error cannot be retroactively assigned that cause. Successful
repeat runs and comparator-UB fixes do not establish that this failure is fixed.
No retry, skip or increased timeout here conceals it. Follow-up must investigate
reply loss and supervisor status using the evidence already recorded in #645.

The original implementation tasks through #642 are accepted. #643 is accepted
only after this PR's complete CI and merge; #542/#555 must remain open while the
#645 blocker is unresolved. Historical checklist entries are evidence, not extra
active tasks or current runtime restrictions.
