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
