# Runtime ownership and tracing (GC-01)

## Two separate contracts

**Ownership**: a returned owned value, or a reference retained using `INCREF`,
must be transferred or released with `DECREF`. Borrowed references require their
owner to remain live. Many static singletons are process-independent and have no
mutable reference counter. An ordinary last `DECREF` releases outgoing owned edges
and frees or pools the shell. Pooled shells are not live objects. Weak self-edges
do not participate in ordinary refcounting. Module identities, bytecode, native
descriptors, OS handles and native allocator blocks have their own lifecycle;
they are not Goat heap objects merely because a live Goat value refers to them.

**Tracing**: a positive refcount, an owned C local or a borrowed pointer does not
make a tracing root. A live heap object must be reachable from one of the roots
below when tracing runs.

### Checked strong counts (GC-06)

Dynamic integers, reals, characters, strings, arrays, user objects and
functions use one 32-bit unsigned strong count under the execution lock
(or exclusive ownership), not atomics. A new or reused object starts at one.
Zero is reserved for inactive pooled shells or objects on the finalization
path; it is not a retainable state. Retaining at the upper limit or
retaining/releasing a zero live count terminates the process before modifying its
count, in both Debug and Release. No count wraps or saturates. An already
`DYING` graph object is skipped by the existing nested-release guard.

Tracing reconciliation is not an ordinary `DECREF`: it removes an excess
dead-source reference only while the marked target's count is greater than one
and cannot trigger finalization. Static singletons have no managed count;
module identities, native descriptors and other distinct resources keep
their own counters and synchronization rules.

### External owning roots (GC-05)

`register_gc_root(process, value)` owns a reference and publishes it as a root.
Each registration has a distinct handle, even for the same object; copying a
handle does not create another registration. `unregister_gc_root(process, handle)`
drops exactly that registration and can synchronously release the value. Locals
borrowing from the registration must stop using the value before unregistering.
Null or foreign objects, registration during teardown or a release drain,
and stale, repeated or foreign handles are rejected without changing ownership.
No root updates are allowed during collection. Process-independent static values
are allowed. Handles do not keep processes alive.

Use the execution lock (or exclusive process ownership) to register, unregister
and collect. The table grows geometrically using the managed allocator's fatal
failure policy, reuses free slots and does not allocate on unregister. Teardown
blocks new registrations and releases all remaining roots before the shallow heap
walk. It invalidates a registration before release callbacks can reenter.

### Last-owner release (GC-04)

An array, user-defined object or dynamic function enters `DYING` and is queued
once when its count reaches zero. The outermost release drains that process's
allocation-free worklist synchronously. Nested reference drops, including
those from a resource cleanup callback, enqueue zero-count graph objects for
the outer drain rather than nesting another native stack frame. Do not
retain a dying object or depend on the finalization order of independent
children. Scalar leaf values retain their immediate release and pooling rules.

The worklist shares the intrusive link with tracing under the execution lock
(or exclusive host ownership); collecting during a release drain is not a safe
point. Links are cleared before finalizing, including before a user-defined
shell enters its pool. Cycle sweep and the final heap walk at process teardown
remain shallow: they do not drop already-swept object references. Weak
self-fields still do not own their target. Module/domain/native resources
are released by their existing cleanup paths.

### Completed program workers (GC-07)

A worker drops its context chain, operand stack and any queued callable before
publishing completion under the execution lock. The record keeps its identity,
successful return value (including object identity), or exception value and
original trace, plus status, observation and native diagnostic counters.
The existing stack-root walk sees the outcome via a fixed, inline zero/one-item
view, not discarded execution state. Late, repeated and
concurrent joins have the same outcome; a join does not consume it. Unobserved
failures are still reported for the current execution. The host entry thread
keeps its own scope and operand stack for the REPL and embedding APIs.

Completed OS workers are reaped once, outside the model lock at a pre-existing
VM scheduling transition, or during final shutdown. A worker completing after
a transition is claimed at the next one; a long native segment with no such
transition may delay reaping. Shutdown still waits for every descendant,
including those created after entry finishes. The runtime keeps completion
records and their results until process teardown; a lifetime bound would need
a separate language/API decision, since thread identifiers carry no ownership
and can be joined again at any later point.

### Inactive object pools (GC-08)

Each process can retain at most 1,024 inactive shells of each pooled type and
512 KiB of Goat allocator payload across all pools. The payload counts scalar
and string shells, and user-defined shells plus four vector headers and
backings (prototypes, topology, keys, fields) and the empty AVL header. String
text, property cells, AVL nodes, access identities, and native StringBuilder
buffers do not remain in the pool. Vectors with capacity above 64 pointer slots
lose their backing on return; smaller buffers retain their allocation for
reuse. A shell that would exceed either pool limit is freed instead.

`trim_process_object_pools` explicitly releases all inactive shells and returns
the exact number of accounted bytes released. Call under the execution lock or
with exclusive process ownership. It does not allocate, mutate live objects,
or run tracing. This is a conservative fixed retention policy, not an RSS limit
or a memory budget calibrated to every deployment. The stress and small-object
timing workloads are documented in `test/data/gc-pool-budget/README.md`.

### Observing existing passes (GC-09)

A host or test can attach caller-owned `gc_stats_t` using
`configure_gc_statistics` under the execution lock or exclusive process
ownership. The caller reads under the same rule and keeps the storage alive
until detaching or destroying the process. Diagnostics are off by default,
allocate no buffers and emit no output. No new collection call, safe point,
trigger or cadence is introduced.

The last pass distinguishes the existing end-of-run call from explicit
host calls. It records distinct marked managed objects, outgoing edge
visits of marked graph objects (including weak and static targets, excluding
root enumeration/reconciliation), before/after active and pooled payload
bytes, and the net drop in their sum. Transferred shells are not counted as
freed if retained in a pool. The monotonic pause includes diagnostic walks
and has a validity flag. Explicit pool trim adds separate call/byte totals
without recording a tracing pass; teardown is not an explicit trim.

The active payload is the managed shell and its directly owned backing:
strings, elements, user vectors/cells/index and StringBuilder buffer, and
function arguments/access snapshot allocations. Shared module/domain/native
resources, thread state, allocator overhead and RSS are outside this metric.
Auxiliary payload reports the root-slot and standalone string-cache tables;
the intrusive tracing worklist allocates no separate buffer. The reproducible
workload is described in `test/data/gc-statistics/README.md`.

## Audit of owners at a potential collection boundary

| Owner / edge | What retains it | Tracing and release | Synchronization |
|---|---|---|---|
| Process launch directory (`process.c`) | Process creates and owns the directory | Direct mark; release during process teardown | Initialization before publication, then model lock or exclusive host access |
| Standalone bytecode string cache (`vm.c`) | Cache entries own materialized strings | Direct mark; drop entries before end-of-run GC | Execution lock |
| Module image and file state (`module_files.c`) | Process owns per-image caches, scopes and recorded failures | Mark all cached strings, scopes and failure values; the process releases its heap before destroying the image backing storage and exception traces | Model lock for publication, file initializer gate for init order; the initializer gate does not replace the model lock |
| Entry scope for declaration reservation (`vm.c`) | `vm_execution_t.entry_scope` temporarily retains the launch scope | The prepared launch file scope is also held and marked by the process module table; clear reservation state after workers finish. The borrowed struct itself is not scanned | Execution lock |
| Thread bootstrap slot and operand stack (`thread.c`, `vm.c`) | `start_function` owns the pending callable, the stack owns queued arguments and other values | Mark both independently. Under the lock the worker moves the slot into a C local, clears it, dispatches and drops ownership. Failed creation destroys the thread and releases slot/stack | Thread publication, worker takeover and execution share the model lock; the takeover sequence is not a new safe point |
| Thread context chain (`context.c`, `access.c`) | Context owns its scope, immutable arguments, explicit receiver, pending return/throw value and lexical access snapshot | Mark scope, arguments, receiver, pending value and access lexical scopes/captured storage; destroy on unwind, frame exit or teardown. The immutable shared root context has no access snapshot to scan | Execution lock |
| Thread exception and join state (`thread.c`, `vm.c`) | An active thread owns its exception; a compact completion owns its result or exception and trace | Mark the payload or successful worker result, not the separate trace. No worker context or operand stack after completion; retain outcome for later joins and diagnostics | Execution lock; condition wait releases the lock |
| Constructor snapshot (`init_snapshot.c`) | Context owns the snapshot and its handle for an immutable array of receiver and callables | Unregister on completion, unwind, or thread teardown; the registration owns the array | Execution lock |
| StringBuilder storage (`string_builder.c`, `user_defined_object.c`) | The builder receiver owns the native Unicode buffer; clone/flat copy it, not its pointer | Raw character storage has no Goat edges to mark. Clear the buffer and pointer in object cleanup, including sweep and pooling. The published length is an ordinary traced field; a builder initializer captured by the constructor follows the snapshot row above | Receiver model operations share the execution lock |
| Function and object graph (`function.c`, `array.c`, `user_defined_object.c`) | Functions retain closure, names and access captures; arrays retain elements; user objects retain prototypes and property edges per type contract | Object mark callbacks walk strong graph edges; normal last release drops owned edges. Pool list entries are not scanned as live values | Execution lock or exclusive host access |
| Native continuations and builtin operator invocation (`function.c`) | Each invocation holds its own handle to the popped callable or to the receiver/argument aggregate | Unregister after the call; other live VM operands are on contexts/stacks, not implicit C roots | Model lock and existing synchronous invocation contract |
| Native ABI descriptors and per-call state (`module_native.c`, `native_services.c`) | Descriptor retains independently loaded code; host owns per-invocation ABI resources | ABI scalars and memory-service blocks are not traced Goat objects. Host cleans up on return, failure or fallback; any Goat object across a possible GC needs an ordinary traced root | No generic callback or allocator safe point |
| Explicit host roots (`gc.c`, `launcher.c`) | Each handle owns its registration (e.g. REPL session scope) | Trace registered values and release any remaining roots before the heap is disposed | Execution lock or exclusive host access |
| Temporary C values (`vm.c` and model helpers) | A local may own or borrow a value; `operation_result_t` carries a value, with trace handled separately | Not scanned. Publish every live managed value into rooted state before any collection; local transfer/clone tables, popped operands, return values, pending callback results are not implicit roots | Model lock serializes graph mutation, but taking it does not by itself make a safe point |

In the root scan an empty thread list means there are no thread roots: process and
module roots are still marked. This does not authorize tearing down a running
worker. The collector walks one process's allocation list; strong graph edges
must stay within that process, except for process-independent immutable objects.
Do not use registration or direct pointer assignment to transfer an object
between processes. The loader checks several boundaries explicitly, but model
constructors and mutators do not uniformly validate ownership; this is a contract,
not a runtime guarantee. #556 tracks model ingress checks. #552 assumes a
single-process graph; #556 gates paths that can admit foreign objects.

## Collection boundaries

A managed object on the process list starts unmarked, becomes marked if reached,
and surviving marks are reset for the next pass. A type may use an intermediate
dying state while releasing edges and a zombie state for an empty pooled shell;
neither is an accessible live value. Ordinary last-reference chains use their
separate iterative release worklist (#546).

Mark uses a process-local LIFO worklist, not the C call stack. Arrays, user
objects and dynamic functions each reserve one transient pointer; leaves need
no worklist link. Mark when enqueuing, pop and clear the link before visiting
immediate edges, and visit each reachable container at most once. All roots
feed the same worklist. Tracing and refcount reconciliation use one edge inventory:
a weak self field remains visible to tracing but does not contribute a count.
The worklist performs no allocation, so allocation failure cannot leave a partial
mark followed by sweep. Reconciliation and sweep start only after it drains.
Once marking is complete, but before any object is freed or survivor mark cleared,
each unmarked container/function subtracts its owned references to marked objects
in the same process. Each owned edge is counted separately; weak object self
fields and borrowed lookup topology contribute no references.
This pass does not invoke normal last-reference destruction: every marked target
keeps at least one reference from its traced owner. Dead-to-dead edges are not
dereferenced after reclamation begins. Sweep then only resets marks and discards
unmarked objects via their type-specific shallow cleanup. No phase admits recursive
collection.
Process teardown is separate: it assumes execution has ended, destroys thread
records and forcibly releases the heap and pools. Its success does not prove safe
collection during execution.

`collect_garbage` does not acquire `execution_mutex`. Its caller must exclude
other mutators: an exclusive non-executing host is sufficient, otherwise the caller
must hold the execution lock **and** all participating execution paths must have
published their live managed values. No concurrent or recursive collection of the
same process is supported. The lock is recursive for VM re-entry, not for GC
re-entry. An initializer gate or an allocator lock cannot substitute for this
contract. No GC is allowed in arbitrary allocation, native or blocking calls,
during context/stack transfers, or between individual stores in an operation.

### Safe-point audit (GC-10, partial)

The production call remains the end of `finish_string_cache`: all program
threads and descendants have completed, their OS handles have been reaped,
and the standalone cache has been dropped. The caller holds the execution
lock. This audit does not certify a general in-run collection boundary.

| Boundary | Required ownership and synchronization | Evidence or outstanding work |
|---|---|---|
| Host outside execution | The caller exclusively owns the process; live managed values use known roots, not C locals | Model-only collection is supported under that contract |
| VM instruction boundary and worker handoff | Stack/context changes and worker result publication complete under the execution lock before roots are inspected | Queued-callable test covers startup. The production end-of-run test keeps a heap result/exception and original exception trace solely in the completed worker records; finishing an opcode does not certify other suspended paths |
| Native continuation step, native fallback or builtin callback | The execution lock does not certify C-local owners; the callable and relevant receiver/argument aggregate use explicit owned handles; callback result must be published or protected before inspection | The existing selected continuation/constructor/builtin probes are controlled tests, not permission to trace in arbitrary callbacks or fallback |
| Pending exception, finally and constructor transition | Root the payload in thread/context state, keep saved return/throw values and the initializer snapshot live until the transition commits; trace metadata has separate ownership | A controlled native fixture observes fresh heap return and exception payloads through finally; test statistics distinguish the known probe from normal end-of-run. Not every transfer or VM/native combination is audited |
| Join/wait, blocking host I/O, loader or OS reap | Releasing the lock is not a safe point. A permitted test first publishes every live managed reference and prevents mutation until the collector takes the lock | Controlled test-host wait verifies one published result, stable roots and restored recursive lock depth. Arbitrary native/I/O/loader suspension has not been certified |
| Module readiness and initialization gate | Publish scopes/results with the model lock; initializer mutex does not replace it. Existing module table keeps committed values alive | A general protocol for partially completed inclusion/loading still needs verification |

Before adding permitted in-run points, finish the publication inventory for
temporary operands, operation results and callback/initializer transfers,
then require all participating mutators to be in known stable states and
exercise each admitted boundary under the intended protocol. #556 gates
ingress that can admit objects from another process, not ordinary same-owner
graphs. This partial audit does not complete #552 or unblock #553.

### Proposed admission contract for in-run safe points (GC-10)

Admission is per boundary and per process, not per call stack. Until a boundary
is reviewed and verified against this contract, it remains forbidden for in-run
collection. The supported host-exclusive and production end-of-run cases above
are unchanged; controlled test fixtures are not production admissions.

| Candidate boundary | Designated owner of managed values at admission | Required stable state |
|---|---|---|
| VM dispatch, unwind and constructor transitions | Thread stack/context owns operands and pending outcomes, including return and throw values; the constructor snapshot owns its receiver/callables until completion or unwind | Transfers are committed; values are visible from their final owners or explicit owning roots, never solely from a C local |
| Native continuation, builtin callback, fallback or VM re-entry | The invoking frame owns required callable/arguments; an explicit owning root or committed caller-visible state retains each live managed result | Each nested invocation has published its own live values. A nested call does not inherit admission from an outer call |
| Worker launch, wait and resume | The published thread state owns startup values; a parked frame or explicit owning root retains suspended values; the completion record owns the outcome after completion | Ownership is established before suspension or handoff and retained until the receiving state owns the values |
| Module/include loading, initialization or waiting | The module table owns committed state; the initiating frame or an explicit owning root retains provisional managed values and pending outcomes | Publication and synchronization are coherent with the process model lock; readiness gates alone do not grant admission |

The collector must have exclusive model access for the entire pass. Every other
mutator of that process must be demonstrably quiescent with its managed state
published; resumption cannot overlap the pass. Holding the execution lock is
necessary for the proposed in-run case but does not prove quiescence or
publication by itself. No wait for quiescence may hold a lock needed to make
that quiescence possible. Recursive VM entry is not collector re-entry:
concurrent or nested collection is forbidden, including during refcount drain
and teardown.

To admit a point, record the participating paths, owning locations and transfer
lifetimes, and the lock/wait ordering that guarantees quiescence and resumption.
Then demonstrate those invariants with normal positive tests at the proposed
controlled boundary, including simultaneously parked participants where
applicable, on success and exceptional completion; run the relevant regression
suite and required CI. A path without that evidence stays excluded. This
proposal does not add a test hook or runtime entry point, enable periodic or
allocation-triggered tracing, change #556, complete #552, or unblock #553.

#### Implementation order and acceptance for the admission gate

1. Start with a private, non-waiting decision under the execution lock.
   A refused request must leave the model and pending result unchanged: it
   must not release the lock, wait for another thread, or enter tracing. Keep
   the host-exclusive and end-of-run calls separate from this decision.
   First exercise the decision without collecting. Later, a bounded test
   request may invoke one existing tracing pass after admission; no runtime
   timer, allocation threshold or default test request belongs in this step.
   A declined or unused test request cannot carry over to the next run.
2. The first candidate is the outer dispatcher of a standalone `run`, after a
   successful opcode returns and before `execute_instructions` calls
   `schedule_and_reap`. Start with one live entry mutator, no worker started
   in this run (`first_worker == NULL`), and no other model clients. For the
   first test, use `exec_ARRAY`: it puts the result on the operand stack
   and advances the instruction index before returning. Require the top-level
   dispatch path, not merely lock
   depth or `active_threads == 1`: `run_module_file`, include, continuation
   stepping and nested VM calls can reach similar control flow. Do not admit
   while unwinding, during refcount drain or teardown, or after completion of
   the dispatch loop. The first test should retain a heap result only through
   a traced operand slot and verify normal execution after one admitted pass.
3. Extend admission to other participation states one at a time. A worker
   published by `spawn_program_thread` has a callable and arguments in traced
   slots until `program_thread_entry` takes the callable. Its takeover and
   initial dispatch are not themselves safe points. Before adding parked
   participants, track their publication under the model lock: a caller must
   move every live result from its C locals to traced state before recording a
   stable park. Pending values in contexts, constructor snapshot registrations,
   module scopes/failures and completed worker records already have different
   owners; do not keep redundant roots indefinitely. On resumption, leave the
   parked state under the model lock before changing the graph. A refused
   request is deferred; never wait for readiness while retaining either the
   model lock or a gate another participant needs.
4. Classify each existing lock-release/wait path before permitting it:
   `join_program_thread`, `schedule_and_reap`, file initialization and loader
   waits, native calls/callbacks, and blocking builtins. A recursive native
   callback or include inherits no admission from the enclosing frame. In
   particular, `static_call` moves popped arguments into a C array, and a
   native invocation may hold borrowed views while its model lock is dropped.
   Leave such paths excluded until ownership has been shown across each
   transfer, including exceptions and fallback. No raw suspend/resume or
   condition-wait helper should implicitly advertise a safe point.

The #580–#584 fixtures verify specific publication paths, not the gate:
controlled waiting, outcomes through finally and completion, same-process
module initialization/wait, and a real queued worker before bootstrap. A
completion claim for #552 needs a matrix of *implemented, admitted* points:
each row names its ownership handoff, possible concurrent participants, refusal
conditions, and success/exception tests. Prove that a refused request does not
collect, an admitted request collects exactly once, and every normal wakeup or
error restores the participant state. Include at least one deterministic test
with more than one parked participant and a documented lock-order argument;
do not force tracing inside a refused state to test the refusal. Require
relevant regression suites and CI on those changes. Until this exists, #553
must not schedule collection during execution.

### Checked process ingress (#556)

A managed reference belongs to exactly one process. Ingress accepts a live object
with the destination `process`, or a process-independent immutable static object
(`object->process == NULL`). This is a model/C API contract, not a proof that an
arbitrary pointer is valid: callers must supply live objects and hold the model
lock or exclusive ownership. Direct writes to public internal structs, vtables,
field cells or stack buffers bypass the API and remain unsupported.

| Boundary | Check and rejection before mutation |
|---|---|
| Mutable/read-only array construction | Validate the complete element batch; return NULL without allocating or retaining any element |
| Array insertion/index replacement | Validate values against the container owner (and explicit process on index writes); return InvalidArgument without changing capacity, length, elements or refs |
| Object construction/prototypes | Validate all prototypes before allocation or pool removal; return NULL |
| Field creation, replacement, pending initialization | Check retained keys/values; return NULL or MSTAT_FOREIGN_OBJECT. A rejected initializer leaves pending/constant flags intact. NULL preflight values remain supported by property assignment |
| Function construction | Check closure and every parameter name before taking ownership of the names buffer or retaining values; return NULL, leaving that buffer with the caller |
| Access snapshots/captures | A function/context binds its snapshot to its process. Standalone snapshots bind on their first managed storage. add_lexical_scope, add_function_field_capture and inherit_function_access return false before changes for another owner; module identity is not heap identity |
| Context/thread construction and argument snapshots | Check caller/prototypes, context data or the complete argument batch before publication; constructors return NULL, capture_context_arguments returns false |
| Operand stack | The stack has a borrowed immutable owner. A rejected push returns BAD_STACK_INDEX and consumes no reference; replacement returns false before releasing the old value. Valid push transfers ownership; valid replacement retains the new value |
| Spawn | Check callable and every argument before declaration reservation, thread creation, stack pushes or INCREF; return InvalidArgument. Normal execution eligibility checks still apply first |
| Builtin/dynamic function dispatch | Reject a foreign callable/receiver before popping arguments or creating a frame; return false with InvalidArgument |
| Shallow clone/flat/function adaptation | Reject a foreign composite/function before making a copy; no implicit heap transfer. Scalar clone implementations can explicitly copy independent payloads into another process |
| External GC registrations | Existing register_gc_root ownership check continues to reject foreign values |

Internal field copying, lexical transfer maps and direct stack rearrangements
operate only on already validated same-process graphs. They are not a public
cross-heap transfer facility; their source/destination provenance comes from the
checked clone/flat, function, context and stack entry points. Module identities,
access domains, bytecode/native descriptors and exception traces have their own
resource ownership, not GC-heap ownership. Borrowed lookups/comparisons do not
create retained cross-heap edges.

`core.gc_ownership` uses two real heaps, checks batch rejection (including a valid
prefix), unchanged refs/heap allocation/stack/field state, same-owner and static
successes, and clean destruction. Existing operator, module, thread, native and
REPL suites cover valid callers. Tests never invoke tracing on deliberately
invalid graphs. These checks do not admit any new collection point or complete
the still-partial GC-10 publication protocol.

### Dedicated post-ARRAY dispatcher test (#588)

`core.gc_dispatch` compiles a separate VM with `GOAT_TEST_GC_DISPATCH`. Its
private host API requests at most one pass per standalone run. The ordinary
interpreter, CLI and `run_module_file` have no request switch. The host must
own the process exclusively throughout the run; a boolean assertion is not a
production participant-registration protocol.

| Boundary | Current implementation | Evidence / refusal |
|---|---|---|
| Standalone outer post-ARRAY | Test host only, before scheduling | Initial context, dispatcher depth one, normal and pending flow, published operand result, model lock depth one, no workers or other clients, plus the admission predicate |
| Nested function, include or continuation | Excluded | Initial-context / dispatcher provenance fails, or the runtime has no request |
| Module entry, worker and parked participants | Excluded | No request is installed; no concurrent participation protocol exists |

The request is consumed at the first ARRAY even on refusal. The decision does
not collect, wait or drop the lock on refusal (ordinary scheduling still runs
afterwards). A request lives only in the current runtime; an unused request
cannot affect a later ordinary run. Tests verify one explicit pass despite two
ARRAY instructions, reclamation of an unreachable cycle before the observer,
a stack-only nested array result, subsequent normal execution and an exception
carrying that result. Separate cases cover recursive locking, missing exclusive
ownership evidence, nested function execution, pending flow, no ARRAY, process
reuse and rejection before dispatch. Final run-end GC is counted separately.

This implements only the first dispatcher experiment of #552. Multiple parked
participants, native transfers and module/wait lock ordering remain unproved;
#552 stays partial and automatic scheduling in #553 remains blocked.

### Real worker refusal at the dispatcher (#590)

The dedicated host also runs four `core.gc_dispatch` worker cases: two workers
waiting inside builtins, or both already joined/reaped before the first ARRAY;
each case covers ordinary results or observed exceptions. The workers retain
heap arrays only in C locals while waiting. Those values have refcounts but no
traced owner: waiting alone cannot authorize collection. No tracing is forced
inside the refused state.

The barrier uses only `threads_changed` and the process model mutex. Each
worker publishes its arrival under that mutex and waits in a predicate loop;
the entry thread waits until both arrivals are visible. The condition wait
releases the model mutex, allowing the remaining participants to progress.
After the ARRAY refusal is observed, the entry thread sets the release flag,
broadcasts and joins. In the joined variant this release/join happens before
the first ARRAY. Normal dispatcher scheduling reaps the workers before the
inspection callback, which verifies `active_threads == 1`, an empty completion
queue and released OS handles. There is no second gate/lock, sleep, or reliance
on scheduling speed. CTest limits each case to 30 seconds.

The tests require zero collections before resuming the unpublished C-local
values, intact return/exception payloads, restored lock depth and completion of
both workers. A fresh run on the same process must admit exactly one pass:
worker history belongs to the execution, not the lifetime of the process.
The joined case specifically protects `no_worker_started`; a count of one
active thread is insufficient. These are refusal tests, not a safe publication
protocol for parked workers. #552 remains incomplete and #553 blocked.

### Controlled publication of waiting workers (#592)

The dedicated test host now has an opt-in `published_workers` request. This
does not enable production GC or automatically treat builtin/native waits as
safe. The reviewed fixtures use a static zero-argument builtin without a
receiver or enclosing native/module frames; its sole live managed C-local
value is the array passed to `gc_test_publish_park`. A more general builtin
would need to publish its entire live call chain first.

Under model-lock depth one, publication registers an owning GC root and then
links a stack-scoped park registration into the current execution. The caller
retains its original reference. Duplicate registration, recursive lock entry,
and mismatched/double resume are rejected. At the real outer post-ARRAY site,
the dispatcher checks its normal mutator conditions, then requires one
registration for every current live worker and an exact active-thread count.
An unregistered, finished, initializing, failed or joining worker blocks this
route. It does not wait for missing registrations. The original no-worker
route and its historical-worker refusal remain intact.

The barrier and registrations share the model mutex. Publish before signaling
arrival and waiting; condition wait releases the mutex. Spurious wakeups only
recheck the release predicate. On actual resumption, unlink the stable state
before removing its root, under the same mutex, before touching the graph or
returning/throwing. GC holds this mutex throughout tracing; there is no second
lock or readiness gate to invert the order. No token or root survives worker
completion; a new run has an empty registration list.

| Test-host participants | Post-ARRAY result | Verified outcomes |
|---|---|---|
| Two published waiting workers | One tracing pass | Dead cycle reclaimed; both published arrays and entry operand survive; return and exception; roots removed after resume |
| One published and one ordinary waiting worker | Refusal, zero tracing passes | Both workers resume and return/throw intact; registered root removed |
| Two ordinary waiting or joined/reaped workers | Refusal as before | Existing #590 scenarios unchanged |

All variants also verify a fresh run on the same process. These controlled
fixtures demonstrate a publication handshake, not a complete runtime protocol:
arbitrary native frames, module/include waits, join publication and worker
bootstrap still have no admitted collection point. #552 remains partial and
#553 must not schedule automatic collection.

### Withdrawn and republished participants (#594)

Additional dispatcher scenarios withdraw one worker registration before the barrier, replace its C-local array and either wait without publication or republish the replacement using the same token. The former must refuse tracing; the latter must collect once and preserve the new array. Both return and exception outcomes verify root cleanup and a fresh process run. This covers registration lifecycle, not arbitrary native/module frames.

### Batch publication (#596)

A fixture may publish a nonempty array of independent live values with `gc_test_publish_park_values`. The whole batch is checked for process ownership and valid entries before roots are registered. Every owning root exists before the participant becomes stable. Resume withdraws stability first, then releases every root and the handle array. The single-value wrapper retains its contract. OOM follows the existing fatal allocator policy; this is not OOM recovery.

Worker scenarios now retain two independent arrays, exercise an invalid second element without partial registration, and verify both arrays after return/exception, refusal and republication. General native/module call-chain publication is still not established.

### Builtin arguments across a published wait (#598)

The controlled worker fixture now also covers direct static builtin calls with two heap arguments. `spawn_program_thread` retains them on the startup stack; the spawning host releases its references. `static_call` pops both into its C argument array before entering the builtin (the test requires an empty operand stack). The builtin publishes those arguments together with both local arrays before waiting. No enclosing native/module frame or receiver is involved. This extends the earlier zero-argument fixture contract only to these explicitly published arguments.

After resuming under the model lock, the fixture withdraws registration, checks both arguments and returns or throws the first argument with a new owned reference. The ordinary static_call cleanup releases the argument array and its references. Both the published and mixed/refused variants check final payloads, root cleanup and a fresh run. Arbitrary builtin call chains are still excluded.

### Explicit outer builtin frames (#600)

A controlled outer static builtin now roots its arguments and an independent local array in an owning read-only snapshot before calling the waiting builtin through call_object. The inner call publishes its own values and park state. The outer C frame stays suspended and its root stays registered throughout the wait; it must not inherit publication merely because the inner frame is parked.

After inner completion, the fixture restores the instruction index, checks the outer graph, takes the returned operand or transfers the plain builtin exception value, and then removes the outer root. Direct builtin exceptions here have no VM trace; the fixture asserts this restriction. Return/exception and mixed-refusal scenarios verify full root cleanup and a new run. This proves only the explicitly rooted two-frame test chain, not arbitrary native/module frames.

### Published builtin receiver (#602)

The controlled nested fixture also invokes execute_method through call_object with a separate heap receiver. It is deliberately absent from the outer frame snapshot and the operand stack: only the method publication batch makes it a tracing root during the wait. Both arguments, both local graphs and the receiver are published before stable park registration.

The method returns or throws the receiver with an owned reference after withdrawing its park registration. The outer caller checks pointer identity and payload, releases its own receiver reference, and verifies the result remains alive. Full-publication and mixed-refusal variants verify cleanup and another run. This extends the reviewed test contract to explicitly published receivers; it does not authorize arbitrary method/native/module frames.

### Forwarded builtin exception traces (#604)

An operation result owns its value but borrows its trace. The trace owner must remain alive until the receiver retains the trace. A builtin can forward the current pending exception, keeping its trace owned by the thread and returning a separate owned value reference. static_call now retains the trace before releasing arguments or clearing the pending exception. Clearing first caused a reproduced ASan heap-use-after-free.

Nested worker fixtures now forward nonempty traces with a checked origin instruction through both builtin frames, normal/receiver calls and admitted/refused collection. This supersedes their earlier trace-less restriction; arbitrary native/module publication is still unproved.

### Pending exceptions during a published wait (#606)

The nested exception fixtures now create an independent heap array and origin trace before parking. The array is owned only by `thread->exception`, absent from the publication batch, operand stack and outer snapshot. The ordinary thread-root traversal must preserve it during the real post-ARRAY collection. After withdrawing park registration, each worker checks the graph and origin, then forwards the borrowed trace while replacing the old exception value with its argument or receiver. Join and a fresh run still verify cleanup.

The test-only worker publication protocol permits this stable pending exception when its value belongs to the process. It must remain unchanged until resume; its trace is reference-counted, not a managed graph node. The collecting main thread still cannot have a pending exception. This does not admit exception dispatch/unwinding, native frames or module waits. Production admission and automatic collection remain unchanged.

The adjacent native exit callbacks currently produce trace-less errors (or terminate the process); their clear-before-retain code does not reproduce #604. VM result dispatch retains the trace before entering exception dispatch; property-error traces belong to process-owned failed module files. These observations are an ownership audit, not evidence that arbitrary native/module frames are safe to collect.

### Completed native continuation steps (#608)

The dedicated continuation test driver has a `step-gc-` mode which collects after each returned `context->step`, including `CONTINUE`, `COMPLETE` and `EXCEPTION`. Its host exclusively owns one process with one active thread and holds the model lock. The driver stores only scalar PC/status values; ordinary operand/context/exception roots and the invocation's callable handle must preserve managed values. A completed step may already have destroyed its call frame, so terminal results must be rooted before the driver inspects the status.

Seven scenarios cover temporary array elements and terminal results, mixed native/VM operator calls and arguments, receiver lifetime, recursive methods, fresh heap return/throw through finally, escaping exception traces and constructor callbacks. Counters require one real collection per returned step and one terminal status per invocation, distinguish builtin probes and end-of-run collection, and require garbage to be swept before completion. Existing checks retain their no-replay, root cleanup, allocator balance and immutable bytecode guarantees. The temporary-array scenario has no builtin GC probe.

These are controlled single-thread probes at the driver API boundary. They do not collect inside a step's VM-only nested call, certify arbitrary generated native adapters, or publish parked native workers. Production GC admission remains unchanged.

### Failed continuation steps and host cancellation (#610)

The same controlled probe also collects after `FAILED`, including the early invalid-PC return, before the invocation host cleans up the remaining frame. Seven additional scenarios cover invalid PC, adapter failure after an observable effect, host cancellation, cancellation with backend failure, and cancellation during VM and native initializers. A failure generated by the adapter after a continuing step is counted separately from a failure returned by `step`; each returned step still triggers exactly one collection.

The ordinary failure/cancellation calls receive a fresh heap argument with no outside owner. The fixture verifies its payload before and after collection while its call frame remains live. Terminal status counts distinguish language exceptions from backend/adapter errors. Existing checks require exit code 7 for host cancellation, no spurious exception trace on a clean cancellation, no VM replay, cleared constructor snapshots and invocation roots, balanced allocation and unchanged bytecode. These probes do not authorize production collection during cancellation or failure handling.

### Borrowed scalar native strings (#612)

A separate shared-library fixture exercises the real scalar v2 loader and FUNC/CALL path. Two temporary concatenated strings, including a supplementary Unicode character and an extra actual argument beyond the formal signature, stay on the VM operand stack while the adapter borrows their storage. The adapter checks both views before and after a private test callback collects garbage. The callback runs only under the fixture's exclusive single-thread ownership: it verifies that invocation released the model lock, acquires that lock for GC, then releases it before returning.

Ten cases combine five outcomes with ordinary and recursively locked host entry: return the argument view, return a substring view, copy into per-call native memory, fail without replay, or request a permitted pure fallback. Alias and native-owned results must survive the host's subsequent argument/native-memory cleanup. Checks cover Unicode payloads, actual sweep before run ends, invocation/retry counts, lock-depth restoration, root/allocator balance and immutable bytecode. The fixture's native scratch allocation also exercises cleanup on every outcome.

This is not a public callback service or a general native-worker safepoint. Concurrent adapters, module-v1 native calls and arbitrary callback frames remain outside this controlled proof; production GC admission is unchanged.

### Scalar host exception (#614)

A scalar adapter now also requests an invalid host exit before the controlled collection. Pending InvalidArgument survives GC alongside borrowed strings. Returning HOST_EXCEPTION propagates a language exception with native origin; returning OK despite that host error becomes BAD_REQUEST, clears the language value and preserves a native failure trace without replay. Both outcomes are tested at ordinary and recursive lock depth. Production admission is unchanged.

### Runtime collection sessions (#616)

A new lock-serialized gc_session API provides deferred requests, explicit participant publication and nested exclusion counters. Every live model thread must have a published token; missing participants refuse collection without consuming the request. A completed program worker is terminal only after its OS thread has been joined and its worker handle cleared; retained completion values and exceptions remain ordinary GC roots. Suspend/resume never changes publication or exclusion depth. Thread destruction retires its token; outstanding exclusions or destruction of a process with an open session fail fast. Session end requires no participants and no running VM; a new session resets counters and pending requests. Hosts must explicitly promise exclusive model ownership. Tokens are stack-owned and noncopyable.

The controlled standalone dispatcher now owns a common session for the run. Its main token is published only after the existing post-ARRAY provenance check; fixture workers register common participant tokens after rooting their complete batches, and withdraw before releasing roots or resuming mutation. The independent parked-worker census and direct dispatcher collection call have been removed. The legacy read-only admission helpers still describe site evidence and historical single-worker tests; only gc_session_poll decides participant readiness and performs this collection.

The default probe still observes one candidate. An opt-in retry mode issues one request and polls later ARRAY candidates without spinning: an unsafe nested call defers, a reviewed outer boundary consumes the same request exactly once. Counters are copied out before session teardown. Completion, early exit and repeated run end the session; a run or module entry refuses an already open external session rather than entering a competing protocol. Default execution and automatic collection thresholds remain unchanged.

Deterministic concurrent tests use real OS threads and acknowledged condition barriers, including recursive lock depth two. They cover a missing startup participant, two nested exclusions surviving waits, publication, withdrawal, republication and retirement. The collector never infers publication from a released mutex. The VM matrix also covers return/exception workers, mixed publication, nested arguments/receivers, collection after join, and reuse of the process. The former joined-worker refusal is deliberately superseded by terminal participant handling. General VM/native/module/blocking integration remains #617; integrated long-run acceptance remains #618, before enabling #553.

### Forced production dispatcher boundaries (#617, first integration block)

An exclusive embedding host can call `configure_gc_boundaries(process, true, true)` under the model lock while the process is idle. This enables collection requests after completed instructions in the production standalone dispatcher, including ordinary function calls/returns, closures and finally/exception transfers. It is disabled by default and is a stress mode, not the allocation-threshold policy of #553. Disable with `configure_gc_boundaries(process, false, false)`. Configuration during execution or an existing session is rejected.

Each executor enters a nested participant exclusion before running and leaves it only after its result has been transferred to VM-owned state. Recursive dispatcher entry cannot publish over the outer exclusion. Suspension for a scalar native adapter or blocking builtin preserves this exclusion; callbacks can request collection, but polling defers until the enclosing executor returns. A successful pass immediately withdraws publication before the next instruction or scheduler handoff. The session is closed before end-of-run cleanup, including error/exit paths.

Publication requires model-lock depth one, no module image on the current runtime, no initializer depth and no enclosing executor. Live workers without publication defer collection; joined/reaped terminal workers cease to block it. Consequently a long blocking call, recursive host lock, module execution or live unpublished worker can postpone collection indefinitely. Module entry does not start a forced session yet. These are explicit limitations, not completed #617 acceptance: module-v1/initialization gates, native continuations and real worker/blocking publication still require integration. Automatic thresholds remain off.

The production-linked test (no replacement VM or direct collector call) compares enabled/disabled execution for recursive calls, closures, array cycles, return-through-finally, caught exceptions and spawn/join. A 500-iteration cycle workload checks a bounded live object count before run ends. Builtin and real scalar-v2 callbacks verify deferred polling while the executor is blocked, including suspend/resume, borrowed strings, retry, native failure and host exceptions. Recursive host depth two is a negative control. Tests check immutable bytecode, results, root/session cleanup and allocator balance.

### Worker scheduling publication (#617)

Forced standalone execution now registers each real VM worker before invoking its startup callable. Startup remains unpublished, including native or builtin execution. After startup returns, the worker uses the same executor exclusions and reviewed boundaries as the main dispatcher. At a safe boundary publication now spans schedule_and_reap: it is established under the model lock, kept stable during suspension and withdrawn after reacquisition, before any further VM mutation. Other unpublished participants still veto collection. The stack-owned token is removed before completion bookkeeping on success, language failure or cancellation.

The production test now runs two workers creating cyclic arrays, with 200 builtin observations proving that each worker is registered and its executor blocks polling. Both results survive join; process roots/session and allocator balance are checked. This removes the blanket exclusion of all active VM workers at scheduling boundaries, but does not publish join waits, I/O, native adapters or module initialization. Those paths can still starve collection and remain part of #617.

### Published calls, module entry and waits (#617)

The forced production session now also covers direct module-file entry and native
continuation instructions. Both dispatchers use the same executor exclusion,
request, publication, polling and scheduling sequence. CALL/CALLM register owning
roots for the popped callable and receiver before a reviewed dynamic call can
transfer its exclusion to a continuation. Each driver step withdraws publication
before returning to the driver; arbitrary builtin callbacks, operator frames and
worker startup do not inherit this permission. Constructor/reentrant paths with
an enclosing exclusion continue to defer collection.

The direct module initializer contributes one known recursive model-lock frame.
Only that wrapper is allowed in addition to the ordinary depth-one boundary;
additional embedding-host locks still prevent publication. The caller remains in
the context chain, the temporarily displaced exception has an owning root, and
the retained entry scope stays rooted until all descendants finish. Nested
include/import initializers and initialization waiters still veto collection;
this change does not change gate ordering or authorize arbitrary lazy initializers.

A direct builtin `join` with one argument roots that popped argument and publishes
the caller for the condition wait, after target/cycle validation. On wakeup it
withdraws publication and restores the executor exclusion before inspecting the
result or exception. Nested C calls and recursively locked hosts cannot acquire
this permission. The main thread also publishes its terminal wait for descendants
at ordinary host lock depth. Completion records remain roots, and reaping rules
are unchanged.

Tests synchronize workers against actual `waiting_for`/`finished` state, then
require collections and bounded object counts while the main thread remains
asleep. They cover normal join, temporary join handles, worker exceptions and
implicit descendant waits. Module tests cover cyclic workers and a displaced
exception. Module-v1 adapters verify temporary borrowed strings, extra actual
arguments, aliased results, invalid results and recursive lock restoration:
polling inside the adapter is deferred, with collection enabled after VM return.
The continuation matrix reruns 31 existing scenarios under production forcing,
including methods, recursion, finally, constructors, errors and cancellation;
selected cases require collections inside `step`, not merely elsewhere in `run`.
Session/root cleanup, bytecode immutability and allocator balance remain checked.

These rules supersede the earlier blanket exclusions for module images,
continuations and direct join waits. I/O, scalar adapter execution, loader frames,
nested initialization gates and unreviewed C callbacks can still postpone GC
indefinitely. #617 therefore remains open; automatic thresholds (#553) remain off.

### Scalar native and builtin I/O waits (#617)

Reviewed CALL/CALLM scopes now publish during scalar-v2 and module-v1 adapter
execution and during `print`, `println` and `input` I/O. `gc_call_wait_begin`
requires the current call token, exactly one executor exclusion, no initializer
depth and only the reviewed lock depth. Startup, nested executors, arbitrary
builtin callbacks and additional embedding-host locks still fail closed. The
model lock is released only after publication and reacquired before withdrawal
and restoration of the executor exclusion. Publication itself does not collect.

Scalar actual arguments, including extras, remain on the VM stack until result
conversion. Borrowed string views therefore keep their traced owners throughout
the adapter. CALL/CALLM roots retain callable and receiver. Static builtin calls
instead pop their arguments into a C array; reviewed calls now register independent
owning roots for every actual argument until builtin return. This also protects
unused arguments and borrowed print strings. `println`'s intermediate result is
the immutable null singleton; input's decoded line is unmanaged storage until it
is converted to a Goat string after withdrawal.

Native exit services acquire the model lock, pause an existing publication and
restore the executor exclusion before changing the pending exception/exit state.
They republish before releasing the service lock. This restoration uses the
already reviewed token, not the original caller's lock depth: a direct module
call has two lock levels, whereas its service reacquires one. Exceptions remain
ordinary thread roots; terminal process states still veto collection.

Production tests collect inside both scalar ABIs with temporary/extra borrowed
strings and recursive-host negative controls. Seven real-library scenarios park
the main adapter while a VM worker allocates cycles, requiring collection before
the adapter returns, including alias, substring, native copy, retry, failure and
host-exception outcomes. Real pipe tests cover direct and method I/O, 128 KiB
output, line input, EOF and malformed UTF-8. The worker observes an actual published
call, allocates cycles and checks collection before transferring pipe data.
Allocator/root/session cleanup and payloads are checked. The protocol test also
covers nested exclusions, initializer refusal and callback lock-depth restoration.

This supersedes the previous scalar-execution and builtin-I/O exclusions only for
these reviewed call scopes. Lazy initializer gates, loading/resolution callbacks,
worker startup and other unreviewed C chains remain possible starvation sources.
#617 remains open, with #618 acceptance and #553 automatic thresholds still pending.

### Worker bootstrap call scopes (#617)

A worker created by `spawn(f, ...)` can enter a scalar native adapter, builtin I/O
or `join` before executing any bytecode instruction. Previously that bootstrap
had no reviewed call token: even with forced boundaries enabled, a blocking
startup call could prevent every other participant from collecting indefinitely.

Worker bootstrap now uses the same scoped call preparation as `CALL` and `CALLM`.
It registers an independent owning root for the callable after transferring it
out of `start_function`, installs one executor exclusion and enables only the
existing reviewed implementations. Bytecode method calls additionally retain
the receiver; direct join calls retain their argument. Scalar arguments remain
on the worker's VM stack; reviewed builtins root all popped actual arguments,
including unused extras. The scoped token, reviewed lock depth and join flag are
restored before releasing roots. Startup restores its exclusion before processing
success, retry, native failure or host exception, then retires the participant
before completion. No publication survives into completion/reaping.

This does not permit collection in arbitrary builtin callbacks or user operator
bootstrap. Those paths retain their exclusion even across model-lock suspension.
A VM function starts collecting only at its ordinary reviewed dispatcher boundary.
Scalar callbacks still pause publication before mutating model state and restore
it before resuming the adapter. Additional host locks and nested unreviewed
executors remain excluded.

The regression matrix runs direct spawned scalar-v2 adapters through seven
outcomes (alias, substring, copy, retry, failure and two host-service errors).
The main VM must collect cycles before allowing the adapter to return. Spawned
module-v1 adapters cover alias, invalid result and host exception with collection
inside the live adapter while the main thread joins. Five pipe cases spawn
`print`, `println` or `input` directly and require collection before transferring
128 KiB of output, a line, EOF or malformed UTF-8. Two join chains spawn `join`
itself and check collection before the target returns or throws. An arbitrary
builtin startup is a negative control. Results, exception propagation, immutable
bytecode, root/session cleanup and allocator balance are checked.

Remaining integration work: lazy initializer gates and waits, loading/resolution
callbacks, and unreviewed nested C chains. These remain explicit starvation
sources; #617 and #618 are not complete and automatic thresholds (#553) stay off.

### Reviewed lazy initialization and gate waits (#617)

`MODULE_INCLUDE` and `MODULE_IMPORT` now carry a reviewed scope through the
persistent initialization gate. A nested initializer temporarily replaces exactly
one enclosing instruction exclusion after its context has been linked to the
caller's chain and its displaced exception has an independent owning root.
Persistent file scopes, cached failures and image-backed strings remain registry
roots. The wrapper accounts for its own recursive model-lock level, restores the
outer exclusion before transferring a result or exception, and never admits an
additional embedding-host frame. An idle `run_module_file` of a non-launch file
uses the same ownership proof without an enclosing instruction exclusion.

The process counts active included-file executors; the session separately counts
reviewed VM executors. Collection requires equal counts. Dynamic
`initialization_depth` is preserved for language restrictions; a separate reviewed
depth permits ordinary dispatcher boundaries and already reviewed scalar/I/O
calls only when the entire initializer chain is covered. `spawn`, `join` and
`exit` restrictions during initialization are unchanged. An arbitrary builtin
inside an initializer remains excluded even when all initializer frames are counted.

The observed gate API pairs notifications under the model lock, immediately before
suspension and after reacquisition. At that point no scope or failure has been
returned, operands and caller contexts are ordinary thread roots, and file records
belong to the registry. Only the reviewed VM observer publishes its participant
and accounts for a waiter. Poll also requires the process and session waiter counts
to match: an unreviewed embedding waiter still vetoes collection. Lock order remains
suspend model, acquire initializer gate, reacquire model; no GC path acquires the
initializer gate. Withdrawal and exclusion restoration precede all file-state work.
Completed/failed files and reentrant inclusion preserve their existing once-only
semantics and cached exception identity/trace.

Eight dedicated cases cover two nested initializers, success/failure, two real VM
workers competing for the same gate, recursive-host refusal and direct non-launch
entry. Collection must happen before the nested initializer finishes; failed-file
retries must reuse the same exception and trace. Five additional real pipe cases
require collection while an initializer blocks on print/println/input (large
output, line, EOF and malformed UTF-8). Module-v1 adapter cases cover collection
inside initialization, alias/invalid results, host exception and recursive-host
refusal. Protocol controls reject unmatched executor or waiter counts. Bytecode,
roots, depths, session state and allocator balance are checked after completion.

Still excluded: generic property-read publication waits (`await_file_publication`),
loading/resolution callbacks and other unreviewed nested C chains. Those paths
can still delay collection indefinitely. The ordinary embedding initialization API
has no observer and remains excluded. This is partial #617 integration, not its
acceptance or permission to enable automatic thresholds (#553).
