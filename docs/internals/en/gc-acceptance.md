# GC acceptance

The [final audit and CI gates](gc-final-audit.md) describe the current automatic
policy, mandatory coverage and remaining blockers. The mixed workload below was
introduced in #637 and deliberately forces boundaries; the acceptance selection
also includes automatic-policy, CLI and pressure stress added through #642.

## Reproducible selection

```sh
cmake --build build --parallel 4 --target gc_acceptance_runners
cmake -DBUILD_DIR=build -P scripts/check_gc_acceptance.cmake
```

The label includes all `core.gc_*`, `core.thread_memory.*` and
`component.init_snapshot.*` registrations (307 tests at introduction). The target
builds every required runner, including the scalar-v2 shared fixture. The existing
Linux GCC/Clang and Windows MSYS2 matrix runs this selection in Debug and Release.
An absent selection is an error. Debug integration tests alone are insufficient:
many scalar/protocol cases have no `integration` label, and Release builds used
an explicit runner list which did not include the complete GC campaign.

## Mixed workload

`core.gc_acceptance.vm` and `.native` run the same module-defined workload. The
native mode binds the module scalar-v1 bridge; the VM mode executes its Goat body.
Each process runs four successive standalone drivers against the retained module:
normal return, caught worker exception, another normal return, and uncaught worker
exception followed by teardown. Each run spawns one worker and blocks the main
thread in real `join`. No sleeps or manually published test participants are used.
The worker waits for that join and verifies its production publication.

Each of 256 iterations creates dead cycles, invokes a constructor, calls a nested
function returning a heap value through `finally`, throws/catches a heap value,
calls the bridge with a temporary string, and uses clone/flat. A fixed live cyclic
array, constructor fields, returned values and exact constructor/finally counts
must survive. The bridge checks its borrowed string before and after a collection
through `gc_session_request/poll`, while the runtime has published the native call.
The C adapter is bound in-process; shared-library scalar-v2 loading is covered by
the other tests in the same selection, not claimed for this bridge.

Every iteration checks the latest in-run collection sample. An arbitrary checkpoint
callback requests GC, releases/reacquires the model mutex eight times and requires
`GC_DEFERRED`, an intact request and unchanged collection count throughout. The
next checkpoint must observe more than ten additional collections; the final
request must also be serviced before run-end collection. No callback calls the
collector directly or fabricates a participant.

The embedding caller selects instruction zero for each new standalone image.
Caught exceptions permit the next run in the same context. An uncaught exception
unwinds the entry context and is followed by teardown, not an implicit restart of
that unwound context. Every return checks session, execution, root and worker-token
cleanup, waiter/reaper state and lock depth. Immutable module/driver bytecode and
final allocator balance are checked too.

`core.gc_acceptance.native-error` returns an invalid native result on bridge call
128, after 127 complete iterations. It requires terminal backend failure, no
retry/fallback, clean tokens/roots and rejection of another run without executing
the bridge again. Native retry and host cancellation remain covered by the
scalar/continuation cases in the same selection.

## Memory budgets

These bounds apply to this fixed-size graph, not arbitrary Goat programs or RSS:

| At each checkpoint | Required bound |
|---|---|
| Last collected and current active object count | Less than 512 |
| Direct active payload after collection | Less than 256 KiB |
| Auxiliary root/cache tables | Less than 32 KiB |
| Current and last sampled pooled payload | At most `GOAT_POOL_BYTE_LIMIT` (512 KiB) |
| Active root registrations / reserved slots | Less than 64 / at most 64 |
| After warm-up (iteration 16) | At most +16 objects, +16 KiB active/pool payload, +1 KiB auxiliary storage relative to warm-up |

The fixed iteration geometry gives the plateau comparison meaning: no growing
application container, increasing call depth or new worker history within a run.
A small fixed margin covers scalar/cache reuse and platform layout differences.
Each test reports peaks and collection counts per run. The CTest timeout is 90 s,
a deadlock bound for the test, not a GC pause guarantee. Arbitrary unreviewed host
callbacks can still delay collection indefinitely as documented in the audit.

## Acceptance evidence and gates

| #552 / #618 criterion | Evidence in the acceptance selection |
|---|---|
| Collections during mixed execution and bounded garbage | `core.gc_acceptance.*`: four runs, 1024 checkpoints per mode |
| Temporaries, finally, constructors, module VM/native transitions | Mixed workload; `core.gc_continuation.*`, `component.init_snapshot.*`, `core.gc_runtime*` |
| Worker startup, join, completion values and reaping | Mixed workload; `core.gc_dispatch.*`, native-startup, thread-memory tests |
| Blocking I/O and native calls, no replay on failure/retry/cancellation | `core.gc_io.*`, scalar-native/native-wait/runtime-native and continuation cases |
| Lazy module waits, mutable graph ownership, observation/copy restarts | `core.gc_module_initialization.*` |
| Deferred requests, recursive C frames, refusals and repeated execution | Mixed workload; `core.gc_embedding`, `core.gc_protocol`, runtime cases |
| Root/refcount/pool integrity and teardown | gc ownership/roots/refcounts/sweep/mark/release/pools/stats plus fixture allocator checks |

Two local mutation controls establish that the new workload is sensitive to the
contract: omitting the pending-finally-value mark produces an ASan use-after-free
in `FINALLY_END`; suppressing join publication fails the worker's admission check.
Both production mutations are reverted before the acceptance run and are not
part of the change.

Current acceptance and closure gates, including unresolved #645, are recorded
in the [final audit](gc-final-audit.md).
