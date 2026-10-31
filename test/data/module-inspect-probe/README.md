# Native inspector probe scenarios (#373)

`inspect_probe_scenarios` migrates `scripts/check_inspect_probe.py` into 48
independent `integration.inspect_probe.*` CTest scenarios. Each owns a build-local
workspace, a cumulative 180-second deadline, retained command output and the common
`--keep-work` interface. Assertions remain active in Release.

The fixture source `main.goat` is copied verbatim from the original Python suite.
`provenance.json` records its origin. `inspect_probe_fixture.c` builds the module
with the real native builder and compiler, extracts its actual descriptor, and
compiles the existing `inspect_probe_library.c` and `inspect_probe_worker.c`.
No host native binary is frozen into test data. Independent format helpers provide
canonical metadata, native/main hashes, normalization and ZIP construction.

## Source-to-scenario map

All IDs below have prefix `integration.inspect_probe.`:

| Original contract | CTest suffixes |
| --- | --- |
| Successful load/descriptor/symbol/unload; no exported function execution | `success` |
| Exact binary captures and 64-KiB truncation boundary | `capture-0`, `capture-65536`, `capture-65537`, `capture-262144` |
| Probe stdin isolation while owner input stays open | `stdin` |
| Crashes, hangs, output-pipe saturation, early zero exit and default deadline | `load-crash`, `load-hang`, `output-hang`, `unload-crash`, `unload-hang`, `load-exit-zero`, `default-timeout` |
| Real malformed descriptor and missing symbol libraries | `descriptor-missing`, `descriptor-mismatch`, `descriptor-overflow`, `symbol-missing` |
| Corrupt native image remains a load warning | `load-error` |
| Explicit current, future minimum OS and opposite-OS compatible target | `explicit-current`, `foreign`, `opposite-os-compatible` |
| Unique diamond graph probes and foreign target no-load behavior | `diamond`, `diamond-foreign` |
| First-found dependency precedence independent of native compatibility | `first-candidate-current`, `first-candidate-foreign`, `later-candidate-foreign` |
| Missing graph dependency and local-only merge preserving all metadata/member bytes | `missing-transitive` |
| Merged graph remains unresolved, then probes after restoring dependency | `repacked-still-missing`, `repacked-complete` |
| Whole-graph validation before any constructor | `invalid-graph` |
| Text escapes binary/terminal bytes and is valid UTF-8 | `text-output` |
| Fault-host start/capture/unload/diagnostic/exit outcomes | `fault-start`, `fault-capture`, `fault-unload`, `fault-diagnostic`, `fault-exit-nonzero`, `fault-exit-hang` |
| Cleaner startup failures admit neither extraction nor constructors | `cleaner-start-exit`, `cleaner-start-silent` |
| Failed public cleanup wait preserves probe result; janitor survives and cleans; fresh explicit inspection retries | `cleanup-timeout-valid`, `cleanup-timeout-load-exit-zero`, `cleanup-timeout-load-crash`, `cleanup-timeout-load-hang` |
| Independent janitor after owner death before extraction | `owner-killed-before-extraction` |
| Pinned descendants stop despite inherited pipes; unrelated worker survives | `tree`, `tree-crash`, `tree-hang` |
| Independent janitor after owner death with live probe tree | `owner-killed` |

Graph scenarios repeat only the prerequisite state needed to establish their
independent assertion. Cleanup-timeout and its fresh invocation deliberately form
one lifecycle scenario: the same event log must contain exactly two constructor
loads, proving that neither the failed invocation retried nor a persistent failure
cache prevented the next invocation.

## Supervision and cleanup oracle

Inspector owners use the shared independent raw OS transport in
`module_loading_owner.c`, not the production child-tree supervisor. Otherwise the
outer test runner could adopt or kill the very janitor whose survival is under
test. Marker barriers and pinned `process_watch` identities coordinate admission,
descendants, cleaner survival and owner termination; there are no sleep-based
readiness barriers. Process pins and owners are scenario-owned for failure cleanup.
The stdin scenario retains an open parent pipe until the inspector exits.

Directory disappearance is a bounded observation: only `FS_NOT_FOUND` proves
success. Known Windows delete-pending access/sharing errors are retried within the
original deadline, never accepted as absence. Persistent denial still fails.
Reports use the shared strict `inspect_report` parser and schema validator;
duplicate fields, malformed UTF-8 and malformed report structures are rejected.

Production execution and fault-hook semantics are unchanged. Validation evidence
is recorded in the migration PR after Debug/Release parity and CI complete.

## Reproduction

```sh
cmake -S . -B build/probe-Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/probe-Debug --target inspect_probe_scenarios
ctest --test-dir build/probe-Debug --output-on-failure -R '^integration.inspect_probe\.'
build/probe-Debug/inspect_probe_scenarios integration.inspect_probe.success \
  build/probe-Debug/goat-inspect build/probe-Debug/module_import_execution_testing cc \
  build/probe-Debug/inspect_probe_testing build/probe-Debug/goat-merge \
  build/probe-Debug test/data . docs/module-format-v1 --keep-work
```

Repeat with Release. CMake supplies absolute executable paths in registered tests;
the direct interface also resolves relative paths before changing directories.

## Local validation and remote gate

The strict original script passed 44 checks before its first `tree` cleanup
assertion failed in this executor, in both Debug and Release. Independent
unchanged tail assertions established `tree-hang` and external owner termination;
`tree-crash` had the same cleanup failure. `/proc/thread-self/children` is absent
here, and the existing production Linux cleanup path treats that error as failure.
A requested local syscall trace could not execute because ptrace is prohibited;
the diagnosis is based on the observed filesystem and source path, not a claimed
trace. No production code or assertions are changed to accommodate this executor.

The final C cases passed 46/48 in Debug and UBSan, and 45/48 in Release: only tree
cleanup was unconfirmed (Release also hit the race-dependent `tree-hang` outcome).
Every other contract passed. Debug's 54 original/C inspector and merge command
transcripts matched argv/hooks/exits/full output after workspace/UUID substitution;
Release matched 53/54, with the actual `tree-hang` cleanup difference preserved.
These are explicitly not claimed as full local green runs. The original Python
validation is temporarily registered alongside the C tests in its originally
supported glibc/Windows Debug/Release CI jobs; it is removed only after strict
remote verification. The musl C matrix runs all 48 scenarios without skips.

Independent negative validation passed 80/80 inspector controls across Debug and
Release: 68 deliberate corruptions and 12 positive controls. It covers schema and
recursive duplicate fields, binary capture/truncation, fault stages, exact exit,
leftover directories, event logs, timing bounds, foreign no-load, and merge output,
normalized metadata and member bytes. A post-pin cleanup assertion failure still
releases the janitor gate and leaves no `goat-run-*` directory in either build.
The extended raw-owner API separately passed 14/14 controls; the existing loading
suite remains the regression gate for its original protocol.

For stronger same-input parity, the unchanged original suite was replayed with
its independently constructed archives replaced by the exact C-generated ZIP
bytes only after every member's hash matched; target JSON was replaced only after
parsed equality. At least 70 archive/profile substitutions in each build matched, with
zero missing fixtures. All original graph, merge, timing and report assertions
before the tree cases passed unchanged. The same local tree-cleanup limitation
remained race-dependent; no diagnostic/state field was removed from comparison.
The final existing module-loading regression passed 94/94 in Debug and Release.

### musl destructor stage

The original probe suite was not part of the earlier musl job. Its first direct
musl execution and the C replacement both exposed the same overly glibc-specific
expectation for two actual destructor faults. [musl's documented dlclose behavior](https://wiki.musl-libc.org/functional-differences-from-glibc.html#Unloading-libraries)
keeps libraries mapped and invokes destructors only on process exit. Therefore
`unload-crash` and `unload-hang` require **exit** plus all four completed stages on
musl, and still require **unload** on the other supported runtimes. The report's
runtime is cross-checked against the runner's compile-time libc classification;
no result-dependent fallback is accepted. Crash/timeout state, events, diagnostic,
cleanup and time bounds remain unchanged. Injected `fault-unload` still requires
unload on every platform. No production behavior is modified.

The musl-stage adjustment passed 5/5 affected real glibc scenarios in Debug,
Release and UBSan. Independent predicate controls passed 28/28: real glibc rejects
spoofed musl or exit-stage reports, and a temporary test-only forced musl predicate
rejects unload stage, missing completed stages and mismatched runtime. Those
synthetic controls do not claim real musl execution; the native musl CI is that
separate runtime gate.
