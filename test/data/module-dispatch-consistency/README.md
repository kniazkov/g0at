# Module dispatch and cross-part consistency (#365)

`test/src/functional/module_dispatch_consistency_scenarios.c` replaces
`scripts/check_module_dispatch.py` and `scripts/check_module_consistency.py` from
baseline `77894149bbdf306ea57ac4a6558ebfcc1a125c34`. The executable is
`module_dispatch_consistency_scenarios`; label:
`module-dispatch-consistency-migration`. The complete
[original-to-CTest map](cases.md) lists every stable ID.

## Cases and oracles

- **Dispatch:** 73 independently selected source/configuration cases (37 auto,
  35 required, one off), plus the real component `--identity` check. The JSON
  corpus preserves the original sources, include files, native adapter actions,
  guard constants/types and expected outputs/metrics. No adapter result is
  substituted by the new runner: the production VM and controlled real ABI
  adapters execute each call. Every case checks exact UTF-8 stdout, including
  embedded NUL, the required exit class, the **first** METRICS line, calls,
  zero retries, optional resolver counts and required terminal diagnostic.
- **Consistency:** 68 independently selected exact binary stdin envelopes. Their
  expected status numbers come from the original handwritten `run(..., expected)`
  arguments, not from recording the host's answer. They retain real archive
  bytes, all native/metadata/bytecode mutations, descriptor envelopes (including
  missing terminator), graph node order/index and dependency hashes. Independent
  ZIP inspection checks framing; semantic corruptions are never silently repaired
  or rehashed. The real production-linked consistency host validates every case
  without opening the deliberately non-executable libraries. Off/auto/required
  all retain their separate registrations, even though their structural path is
  intentionally identical.
- **Consistency API smoke:** a separate ID runs the original no-argument host
  assertions.
- **Producer identities:** one relational ID really compiles the unchanged
  `identities.goat` four times: `(none,off)`, `(all,off)`, `(all,auto)`,
  `(all,required)`. It independently reads GOATPG01/GOATBC01, validates bytecode,
  compares the first six words of all six function records, requires IDs 1–6,
  then really runs `--print-c`. Generated `g_f2_i_`/`g_f3_i_` must be present;
  `g_f4_`/`g_f5_`/`g_f6_` must be absent. Outputs are **not frozen** and no VM
  loader supplies expected identity values.

Total: **144 CTest IDs**. Original printed counts are 74 dispatch and 72
consistency. The latter excludes its API smoke and `--print-c`; all original
operations remain: 74 dispatch + 68 consistency + one API + five producer
commands = **148 actual host commands**.

### Preserved coverage

Dispatch covers per-call fallback, argument evaluation once, missing/extra
arguments, disjoint/equality/NUL-string guards, exact types, borrowed/allocated
strings, no scalar coercion or object conversion, closure and transferred-method
identity, no native entry from failed files, lazy resolver failures, invalid
binding, native terminal errors without VM replay/catch continuation, worker and
initializer terminal behavior, and independent module-instance identity.

Consistency covers byte/length native hash corruption, missing payloads,
metadata/function instruction disagreement, invalid symbols/overlap/CPU/library
extension, future ABI/CPU compatibility rather than corruption, retained
UINT32_MAX function IDs, foreign variants, descriptor identity/target/signature
and nonzero variant index, ignored compiler/payload identity distinctions,
owner-rooted transitive references, versions, missing/duplicate graph nodes,
wrong dependency hash, unreachable/self references and invalid root indices.

There are **no platform skips**. `.so`/`.dll` paths in semantic fixtures are
intentional metadata, never loaded libraries. Passing GOAT explicitly preserves
the original `.exe` platform handling through CMake target paths. Compiler
selection remains the existing inherited `CC`/toolchain behavior.

## Reproduction

```sh
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug --target module_dispatch_consistency_scenarios
ctest --test-dir build/Debug -L module-dispatch-consistency-migration --parallel 4 --output-on-failure
build/Debug/module_dispatch_consistency_scenarios \
  integration.module_dispatch.string-host-allocation.required \
  build/Debug/module_dispatch_testing build/Debug/module_consistency_testing \
  build/Debug/goat build/Debug test/data test/data --keep-work
```

`--list` replaces the ID to list cases. Each invocation owns a unique build
workspace, reads immutable fixture data and preserves failure logs/outputs.
`--keep-work` also retains successful workspaces. Cases share a 120-second
scenario deadline (CTest 240); the five-command producer has a 300-second shared
deadline (CTest 330), preserving the original five 60-second command allowance.
Both corpus files are CMake configure dependencies; root ID keys use canonical
four-space indentation to distinguish them from nested adapter signature `id`.

Repeat with Release. UBSan configuration uses
`-DCMAKE_C_FLAGS='-fsanitize=undefined -fno-sanitize-recover=undefined'` and
`-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined`. Existing Linux x86-64/AArch64,
Windows MinGW i686/x86-64/UCRT and musl Debug/Release targets/labels include this
runner. Exact-head CI is authoritative for full cross-platform completion.

### Release harness dependency

The old dispatch host failed immediately in Release (`invalid source graph`)
because `assert(module_sources_read(...))` and other setup calls disappeared
under NDEBUG. The dispatch identity TU and consistency API/immutability checks
also used assertions. These **three test-only TUs** now undefine NDEBUG before
headers, following existing component harnesses. Optimized production libraries,
formats, schemas and goldens are unchanged. This preserves tests rather than
weakening Release checks.

### Local parity and environment limitation

Both original Python scripts pass against the final Debug and Release hosts:
74 dispatch checks and 72 consistency checks, including actual compilation of
all four producer variants and generated-symbol assertions. To reproduce, obtain
the originals from the baseline, preserving their repository-relative ROOT and
Python helper import paths, and invoke them with the corresponding component
executables. The frozen corpus was recorded with the original assertions active.

The C runner passes **143/144** in Debug, Release and UBSan locally. The remaining
producer case is **not skipped or treated as success**: at the real all-auto
compiler command, the local process supervisor reports exit 0 but cleanup=2
(descendant cleanup not confirmed). One isolated Debug retry reproduces that
existing environment restriction. Retained none-off/all-off/all-auto bytecode
files have the same six IDs and six-word tables. A separate direct compilation
of the fourth all-required variant, independent C binary-oracle validation of
all four files, and generated-symbol checks also pass; these do not override the
failed supervised result. The original Python completes all four compiles and print-c. This is not a claim that the complete C producer
scenario passed locally. Remote CI must pass all 144 before merge; no production
or cleanup behavior is changed to work around the environment.

## Negative semantic controls

Debug and Release reject ten temporary copied-fixture/proxy mutations, each with
exit 1 and a retained semantic assertion (not a setup/launch failure):

1. Wrong native call count in `arguments-once.auto`.
2. Wrong resolver count in `load-failure-auto.auto`.
3. Expected allocated-string stdout truncated at its embedded NUL.
4. Missing required terminal-native diagnostic.
5. Treating corrupted native hash bytes as status 0.
6. Treating descriptor mainHash mismatch as status 0.
7. Treating a graph dependency-hash mismatch as status 0.
8. Adding a seventh retained function to copied `identities.goat` (detected
   before native compilation, independently of the local cleanup restriction).
9. A fake identity host prints `NOTOK` with exit 0.
10. A proxy prepends METRICS with correct calls but retries=1 before the genuine
    valid METRICS line: the first-line/no-retry contract rejects it.

Temporary proxies preserve real host inputs/status and unrelated output. Never
edit canonical fixture files for negative testing. Shared Python compatibility
helpers remain for their explicitly listed consumers until cleanup task #394.
