# Allocation-driven GC policy (#638)

The policy is process-local. Since #639, ordinary CLI source/archive/module runs
(including supported native execution) and REPL sessions enable it with a 64 KiB
minimum. Workers inherit the execution protocol. Compile-only paths do not run
the program. Embedding remains opt-in: create_process/run do not assume that
host C-local references are published. The CLI owns its process exclusively.
`configure_gc_policy(process, minimum_bytes, exclusive_host)` requires an idle
process under its model lock and exclusive host ownership for every subsequent
run. Configuration during an execution/session, collection, release drain or
destruction is rejected. Zero disables and clears the policy; successful
configuration resets counters and samples the initial active heap. The suggested
minimum is `GC_POLICY_DEFAULT_MIN_BYTES` (64 KiB), not an implicit default opt-in.

## Accounted bytes

The debt counts logical activation of managed objects, using their existing
`gc_owned_payload` measurement, plus positive capacity growth of arrays, user
object fields (cells, vectors and index), and StringBuilder storage including
copies. Direct owned payload includes the object shell, not referenced children.
Reactivation of a pooled shell is charged again even without a new malloc.
Freed objects and decrefs do not subtract debt: short-lived allocations still
exert pressure. Zero-sized charges do not count as allocation events.

This is deliberately not a malloc total or RSS. Temporary conversion/rendering
buffers, compiler/native unmanaged allocations, process/thread/context metadata,
root/cache auxiliary allocations, allocator headers, shared module metadata and
later access-descriptor/transfer-vector growth outside these hooks are excluded.
Initial activation includes directly owned metadata measured at that point.
Pool insertion itself does not add debt. No allocation hook allocates, publishes
roots or runs tracing. Hooks share the model's serialization requirements.
Debt, event/collection counters, aggregate heap samples and threshold doubling
saturate at `SIZE_MAX`; they never wrap to a small budget.

## Budget and feedback

The initial budget is `max(minimum_bytes, active_bytes / 2)`. After each completed
collection, including explicit and run-end collections, let `live` be active
payload after the pass. The pass is useful if at least `ceil(before / 8)` active
bytes disappeared and the reclaimed amount is nonzero. This measures removal
from the active graph, not allocator frees: shells may remain in bounded pools.

A useful pass decreases the backoff exponent by one toward zero; a low-yield
pass increases it by one up to four. The new budget is
`max(minimum_bytes, live / 2) * 2^backoff`, with saturating doubling. A completed
pass clears debt and pending state, but preserves cumulative event counts.
The byte interval supplies hysteresis; this is not a time interval or a maximum
pause guarantee. A full synchronous tracing pass still scans the live graph.

Crossing the budget latches one pending request. The next existing session poll
converts it into a normal deferred request; the existing participant/root/lock
protocol alone decides admission. Refused polls retain the request. Session end
without collection preserves debt/pending state for the next session. Explicit
requests work below the budget. The policy works without telemetry. Opt-in VM
and module runs create normal GC sessions without forcing every instruction;
workers and supported native boundaries reuse the existing protocol. Arbitrary
callbacks can still defer collection indefinitely (see [boundary audit](gc-boundary-audit.md)).

## Validation

Eight `core.gc_policy.*` tests are part of `gc_acceptance_runners` and the
`gc-acceptance` CTest label, including existing Debug/Release Linux/Windows CI.
They cover exact threshold/yield boundaries, saturation, process isolation,
pool reuse, mutable payload growth, declined requests across sessions, repeated
VM runs, worker/join/exception paths, module execution and a bound scalar-v1
native adapter retaining a borrowed string. No workload uses force-boundary mode
or injects a collection request. A disabled-policy control preserves defaults.

On Linux x86-64 Debug, a rooted graph of 65,976 active bytes and 16,384 pooled
scalar activations caused four collections; the budget grew to 527,808 bytes.
This checks resistance to repeatedly scanning a large unchanged live graph.
The cyclic workload checks bounded active objects/bytes at ten checkpoints per
run and actual in-run collection progress. Counters and thresholds, not timings,
are acceptance assertions. Tests also check pool/root cleanup and allocator
balance. Platform-dependent byte sizes are observations, not portable constants.
Pressure/OOM, prolonged stress and final acceptance remain #640–#643; this change does not claim to complete those tasks.

## CLI acceptance (#639)

The CLI configures the policy once, before execution, and the REPL retains it
across lines and exception recovery. Configuration uses the existing locked API;
there is no new CLI tuning flag. Hosts may choose another minimum or disable the
policy while idle via configure_gc_policy. Admission restrictions, final GC and
the fatal OOM contract are unchanged.

core.gc_cli tests compile the actual launcher sources with observational run
wrappers and the actual statistics implementation with a pass observer. They do
not enable the policy, change its budget, publish roots or request GC. They assert
the default 64 KiB configuration at entry, more than ten in-run collections,
less than 1 MiB active payload before each pass and 256 KiB afterwards, and clean
session teardown. Cases cover source, saved archive, native-required execution
(with successful native calls), workers and a persistent REPL with an exception.
The ordinary REPL output test independently checks that the surviving closure
returns 42 before and after exception recovery. Measurements are owned managed
payload, not RSS; synchronous pauses and excluded callbacks remain unbounded.

[Explicit host memory pressure and pool trimming](gc-memory-pressure.md) (#640).
