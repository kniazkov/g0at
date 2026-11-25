# Explicit memory pressure (#640)

`request_gc_pressure(process)` is an internal host API, not a Goat exception or
an allocator callback. The host calls it under the process model lock when its
own memory budget indicates pressure, before malloc failure. The allocation GC
policy must already be enabled (including its exclusive-host contract). Disabled
policy, missing lock, collection, release drain, destruction, exit and native
failure reject the signal. This change does not install OS notifications or
infer pressure from global RSS; an allocation-budget request alone does not trim.

The signal only sets a process-local bit: no allocation, tracing or trimming.
Repeated signals before service coalesce. A blocked callback can signal pressure
but cannot publish its C locals or override admission. The existing session poll
requests collection and admits it only when all participants have published.
Nested prohibitions, waiting/starting workers and initialization keep the normal
protocol. Ending a session without collecting preserves pressure for a later
session. Policy reconfiguration is rejected while pressure is pending, preventing
accidental loss or disabling the only executor of that request.

At the next admitted collection, sweep runs first, then all inactive object pools
are emptied. This order also releases shells produced by the sweep. Live objects,
registered roots and required process structures remain intact. Existing explicit
and run-end collections can service pressure as well. The collection guard stays
set through trim, so reentrant signals are rejected. Teardown releases pools by
its existing path and does not promise a pressure observation.

## Bound and observations

One coalesced signal gets exactly one tracing/trim attempt. No allocator retry,
replay of user code or immediate second collection occurs. If nothing is released,
the attempt still clears pending; future idle polls do nothing until a new signal
or ordinary allocation request. A caller may deliberately signal again, but is
responsible for its own rate limit. A permanently excluded C frame can defer work
indefinitely; this is not a time-bounded memory recovery guarantee.

`process->gc_pressure.attempts` saturates at SIZE_MAX and counts completed pressure
passes. `released_bytes` is the last pass's nonnegative reduction in measured
active plus pooled payload; zero is the no-progress criterion. These observations
survive policy reconfiguration. Pending clears only on a completed pass (or process
destruction). GC telemetry includes the trim in freed bytes/pause and records its
existing trim counters. Without telemetry, pressure still works. Payload is not
RSS: libc may retain pages; auxiliary root/cache tables and live capacities are
not compacted. There is no malloc_trim or portable promise to return pages to OS.

## Tests and remaining work

`core.gc_policy.pressure` creates 600 pooled scalars plus an unreachable cycle,
keeps a rooted live array/value, blocks twice and polls 100 times without progress,
ends/restarts the session and verifies one collection empties the pools and
preserves the live value. It checks request coalescing, zero-progress termination,
saturating attempts, forbidden reentry/drain, telemetry and direct final servicing.
`pressure-vm` and `pressure-native` signal from an executing worker while the main
thread joins. They verify one serviced attempt per checkpoint, normal exceptions
and live values, repeated VM runs and native borrowed-value lifetime. They do not
force instruction-boundary collections or replay operations. The existing full
GC acceptance suite remains enabled on Linux/Windows CI.

Actual malloc failure still follows the fatal OOM contract. Fault injection and
that contract's acceptance belong to #641; long pressure stress and final CI audit
remain #642/#643. No catchable out-of-memory exception is introduced.

[Fatal allocation failure and injection tests](gc-fatal-oom.md) (#641).
