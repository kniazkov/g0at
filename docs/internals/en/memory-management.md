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
neither is an accessible live value. Long last-reference chains still use
recursive ordinary release (#546); that is separate from tracing.

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

Production tracing still occurs after `finish_program_threads` and dropping the
standalone cache in `finish_string_cache`. Test callbacks may force collection at
a specifically controlled boundary; they are not evidence of a general in-run
safe point. GC-10 (#552) must audit the remaining per-opcode and native/VM transition
temporaries, suspension/resumption, caught errors/finally and initializer paths,
and enforce/test safe points and re-entry before auto-collection is enabled by
#553. Another ownership boundary requiring verification is cross-process model
mutation; external root input validation belongs to #547.
