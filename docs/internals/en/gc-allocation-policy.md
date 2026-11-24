# Allocation-driven GC policy (#638)

The policy is opt-in and process-local. Normal CLI, REPL and embedding defaults
remain unchanged; enabling the supported paths by default belongs to #639.
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
Default enablement, broader policy coverage, pressure/OOM and prolonged stress
remain #639–#643; this change does not claim to complete those tasks.
