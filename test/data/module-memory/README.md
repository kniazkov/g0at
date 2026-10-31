# Snapshot allocation-failure inputs (#370)

These nine small archives preserve the independent fixture construction in
`scripts/check_module_memory.py` at baseline
`f8da48d6b8c769761eaf4363d27aa06a317804e8`. They are read-only scenario input,
not production-generated output or modified module-format goldens.

The original Python `archive()` function was evaluated unchanged with the
original `reference.main_hash` implementation and original empty bytecode and
native target vectors. Its `zipfile` wall-clock timestamp was fixed to
1980-01-01 00:00:00, making the resulting archive bytes reproducible. All member
names, ordering, JSON serialization, compression, bytecode, native payload,
metadata and significant hashes remain those of the original function. No
Python, reference script or compiler is needed to consume these fixtures.

| File | Original construction / use |
| --- | --- |
| `dep.gbin` | `archive('dep', native=True)`: retained, never executed native specialization; copied to both primary and healthy fallback paths |
| `root.gbin` | `archive('app', [dep_record])`: baseline, every persistent/one-shot allocation failure, success after last allocation, resource limits |
| `plain.gbin` | `archive('app')`: independent source of declared-size overflow archive |
| `count20.gbin` | empty bytecode offset 20 replaced by `UINT32_MAX`, then `archive('app', code=...)` |
| `count24.gbin` | same for offset 24 |
| `count28.gbin` | same for offset 28 |
| `count32.gbin` | same for offset 32 |
| `count36.gbin` | same for offset 36 |
| `huge.gbin` | `plain.gbin` with every central/local uncompressed member size replaced by `0xfffffffe`, exactly as in the original final case |

`fixtures.json` records byte sizes, true expanded sizes, significant hashes and
SHA-256 checksums. `SHA256SUMS` covers all archives. The native payload is the
original deliberately non-loadable text, not a platform-specific executable.
The descriptor's target is deliberately frozen to the original vector target:
this suite preflights/retains native metadata and must never OS-load the payload.

The valid root is 487 bytes / 468 expanded bytes; its dependency is 922 bytes /
1105 expanded bytes. Thus the original exact-boundary limits are 922/1105,
root archive rejection is at 486, root expanded rejection at 467, and dependency
expanded rejection at 1104. Size limits use the **host's** reported `size_t` width,
not the width of the fixture-generating machine. The huge archive's actual
expanded byte count remains 341; only the deliberately invalid ZIP declarations
are huge, so no enormous fixture/allocation is introduced.

To verify the checked-in data:

```sh
(cd test/data/module-memory && sha256sum -c SHA256SUMS)
```

For migration parity, the original Python assertions are replayed against these
exact archive bytes (timestamps fixed as above), preserving all child arguments,
exit statuses, JSON results, fatal messages and allocation-site enumeration.
Allocation counts remain measured per host at runtime, never frozen in fixtures.

## Scenario map and direct invocation

All IDs below have prefix `fault.module_memory.` and label
`module-memory-migration`. The 17 separately selectable scenarios replace
`scripts/check_module_memory.py`; no executable Python helper remains for this
suite. The runner is `test/src/functional/module_memory_scenarios.c`; the existing
instrumented host remains `test/src/component/module_memory_testing.c`.

| Original check | CTest suffix |
| --- | --- |
| valid snapshot executes, two checked archives | `baseline` |
| every measured allocation fails persistently, no execution/fallback, fatal OOM or controlled MEMORY | `persistent` |
| every measured allocation fails once, same contracts | `once` |
| first non-existent failure site executes normally | `after-last` |
| valid SIZE_MAX add/multiply boundaries and host width | `boundaries` |
| fatal checked addition overflow | `add-overflow` |
| fatal checked multiplication overflow | `multiply-overflow` |
| exact archive/expanded limits accept both archives | `limit-exact` |
| root archive one byte too large | `limit-archive` |
| root expanded size one byte too large | `limit-root-expanded` |
| dependency expanded size one byte too large, both checked | `limit-dependency-expanded` |
| impossible instruction/string/file/function/location counts | `count20`, `count24`, `count28`, `count32`, `count36` |
| impossible ZIP declarations, expanded-size sum overflow (including 32-bit hosts) | `declared-expanded` |

Every capture checks sanitizer diagnostics; successful/invalid results require
normal exit, strict JSON fields and no false triggered flag. Allocation failure
requires no `EXECUTED` output, either exit 1 with `Out of memory` or a triggered
MEMORY result; both branches must occur independently in each fault mode. The
host checks no primary-to-fallback retry, snapshot/diagnostic ownership and
allocation balance. Its assertions are explicitly retained in Release: they
also contain execution and cleanup operations and must never disappear with
`NDEBUG`. No production file or compiler flag is changed.

```sh
cmake -S . -B build/memory-fault-Debug -DCMAKE_BUILD_TYPE=Debug -DGOAT_TEST_ALLOC_FAILURE=ON
cmake --build build/memory-fault-Debug --target module_memory_scenarios
ctest --test-dir build/memory-fault-Debug --output-on-failure -L module-memory-migration
build/memory-fault-Debug/module_memory_scenarios fault.module_memory.persistent \
  build/memory-fault-Debug/module_memory_testing build/memory-fault-Debug test/data --keep-work
```

Repeat with a separate `Release` directory/configuration. CI runs both on each
existing Linux GCC/Clang x86-64/AArch64 and Windows MinGW32/MinGW64/UCRT64 matrix
entry. Hooks remain behind the unchanged default-OFF `GOAT_TEST_ALLOC_FAILURE`
option, never enabled in normal production builds. Existing musl and sanitizer
jobs are unchanged.

The shared scenario framework owns a unique directory under the chosen build,
a 300-second cumulative deadline (CTest allows 330 seconds for cleanup), child
tree supervision, complete stdout/stderr artifacts and `execution.log`. Failed
work is retained automatically; `--keep-work` retains successful work as well.
The exhaustive modes measure the current host baseline separately, so allocation
counts adapt to architecture/compiler/path and cannot become stale constants.

## Local validation

The 17 scenarios pass in GCC Debug, Release and UBSan builds. Eighteen deliberately
incorrect child-result controls are rejected in both Debug and Release, covering
zero allocations, incorrect checked-archive counts, absent/forbidden execution,
wrong MEMORY status or trigger flag, vacuous all-controlled failures, wrong
size-width report, bad overflow exit/message, bad fatal-OOM exit/message,
malformed JSON, ASan/UBSan diagnostics, ignored size limits and triggering beyond
the final allocation. Five corresponding positive controls pass in each build.
A real-host negative control with only the healthy fallback present aborts in
`checked()` in both configurations, proving that Release retains the no-fallback
contract, rather than relying solely on a mocked response.

Python/C parity in both configurations compares all 1,305 original invocations
against exact input SHA-256, arguments, exit status and complete stdout/stderr:
645 observed allocation sites per mode, 525 controlled returns and 765 fatal
failures. The C suite makes only eight additional prerequisite calls because
scenarios are isolated. The comparison harness fixes ZIP timestamps and matches
workspace path length and `root.gbin` spelling: absolute versus relative paths
change allocations inside search preparation/loading, so unlike raw elapsed
counts they cannot be compared as though the input transport were identical.
The original unadapted runner also passed in both configurations. Every C fixture
is checked against its manifest SHA-256 before use, including malformed inputs,
so corrupting one rejection fixture cannot silently test a different rejection.
