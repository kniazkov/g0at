# Fatal allocation failure (#641)

Memory pressure and malloc failure are different events. A host pressure request
(#640) schedules one admitted tracing/trim pass before failure. It does not retry
an arbitrary operation. When the common ALLOC/CALLOC allocator actually receives
NULL from malloc, the entire OS process terminates with EXIT_FAILURE (1 on our
supported targets), without a Goat exception, unwinding, finally, atexit, module
unloading or cleanup-service wait. If process exit was already selected by another
thread, the existing first-winner status/deadline arbitration applies.

## Diagnostic and shutdown

Before attempting malloc, ALLOC prepares the process-lifetime exit watchdog.
Preparation is idempotent; it may create one OS thread/event before failure and
later calls observe the cached state. The failure path does not prepare resources,
allocate, collect, trim or retry. It starts the existing one-second output deadline,
writes the fixed ASCII message `Out of memory.` directly to OS stderr, and terminates.
It does not acquire FILE locks or flush buffered stdout/stderr. Buffered application
output may be lost. Partial writes, a closed sink, a blocked pipe or failed watchdog
preparation may prevent delivery; diagnostics are best effort, termination wins.
If preparation was unavailable, output is skipped and termination is immediate.
The watchdog bound is subject to normal OS scheduling, not a hard realtime promise.

Ordinary language exit keeps its existing bounded stdout/stderr flush. Size
arithmetic overflow remains a distinct fatal validation path with its existing
`Allocation size overflow.` diagnostic; this change does not redefine every libc
allocation in the compiler/loader. APIs with explicit allocation-error returns
retain their documented transactional contracts. There is no universal recovery
allocator, OOM exception, successful-retry promise or hidden unfinished retry task.
A future recoverable operation needs its own API, transactional proof and issue.

## Reproducible fault tests

Only GOAT_TEST_ALLOC_FAILURE builds expose the existing thread-local, indexed,
sticky malloc/calloc/realloc injector. Production has no environment/CLI failure
knob. Each subprocess prepares its inputs, emits and flushes exactly one BEFORE
marker, then enables failure at the next allocation. Its successful control proves
that the same operation reaches AFTER and atexit. Fatal cases require normal exit
1 and the diagnostic, and forbid AFTER, ATEXIT, CATCH and FINALLY. Timeout and signal
termination are failures, not accepted OOM results.

Cases cover root registry growth, array storage growth, direct host allocation,
a builtin inside try/catch/finally, an actually bound scalar-v1 native adapter,
and another thread holding stderr's FILE lock. The native failure cannot return a
status for fallback/replay. The intrusive GC worklist requires no allocation: with
failure armed, it collects a dead cycle successfully with zero allocation attempts.
A Unix-only full stderr pipe probe checks that the watchdog terminates blocked raw
output; that probe intentionally cannot receive the diagnostic. Each child is
bounded by a ten-second supervisor timeout. No real machine-wide OOM is induced.

These cases join the existing Debug/Release Linux GCC/Clang and Windows fault-build
step via module_memory_scenarios and module-memory-migration. Tests preserve the
older loader recovery cases instead of changing their expected outcomes. Normal
GC and process-exit acceptance still run separately. Long-running stress and final
cross-platform acceptance remain #642/#643.

Additional subprocess controls cover CALLOC and unavailable watchdog preparation;
the latter must still terminate with status 1 without requiring a diagnostic.
