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
below when tracing runs. `add_gc_root` registers a pointer in a per-process set;
it does **not** retain it. Keep an owned reference, unregister it before releasing
that reference, and do not unregister another owner's registration. Use a separate
owned aggregate when independently nested callers must register the same value.
GC-05 (#547) is responsible for the replacement external registration contract.

## Audit of owners at a potential collection boundary

| Owner / edge | What retains it | Tracing and release | Synchronization |
|---|---|---|---|
| Process launch directory (`process.c`) | Process creates and owns the directory | Direct mark; release during process teardown | Initialization before publication, then model lock or exclusive host access |
| Standalone bytecode string cache (`vm.c`) | Cache entries own materialized strings | Direct mark; drop entries before end-of-run GC | Execution lock |
| Module image and file state (`module_files.c`) | Process owns per-image caches, scopes and recorded failures | Mark all cached strings, scopes and failure values; the process releases its heap before destroying the image backing storage and exception traces | Model lock for publication, file initializer gate for init order; the initializer gate does not replace the model lock |
| Entry scope for declaration reservation (`vm.c`) | `vm_execution_t.entry_scope` temporarily retains the launch scope | The prepared launch file scope is also held and marked by the process module table; clear reservation state after workers finish. The borrowed struct itself is not scanned | Execution lock |
| Thread bootstrap slot and operand stack (`thread.c`, `vm.c`) | `start_function` owns the pending callable, the stack owns queued arguments and other values | Mark both independently. Under the lock the worker moves the slot into a C local, clears it, dispatches and drops ownership. Failed creation destroys the thread and releases slot/stack | Thread publication, worker takeover and execution share the model lock; the takeover sequence is not a new safe point |
| Thread context chain (`context.c`, `access.c`) | Context owns its scope, immutable arguments, explicit receiver, pending return/throw value and lexical access snapshot | Mark scope, arguments, receiver, pending value and access lexical scopes/captured storage; destroy on unwind, frame exit or teardown. The immutable shared root context has no access snapshot to scan | Execution lock |
| Thread exception and join state (`thread.c`, `vm.c`) | Thread owns the exception payload and trace; result remains on its stack | Mark the payload, not the separately managed trace. Finished records still keep their contexts/stacks/payloads until teardown; reducing retention is #549 | Execution lock; condition wait releases the lock |
| Constructor snapshot (`init_snapshot.c`) | Context owns snapshot; snapshot owns an immutable array of receiver and callables | Snapshot explicitly registers the array; remove the root before releasing it on completion, unwind, or thread teardown | Execution lock |
| StringBuilder storage (`string_builder.c`, `user_defined_object.c`) | The builder receiver owns the native Unicode buffer; clone/flat copy it, not its pointer | Raw character storage has no Goat edges to mark. Clear the buffer and pointer in object cleanup, including sweep and pooling. The published length is an ordinary traced field; a builder initializer captured by the constructor follows the snapshot row above | Receiver model operations share the execution lock |
| Function and object graph (`function.c`, `array.c`, `user_defined_object.c`) | Functions retain closure, names and access captures; arrays retain elements; user objects retain prototypes and property edges per type contract | Object mark callbacks walk strong graph edges; normal last release drops owned edges. Pool list entries are not scanned as live values | Execution lock or exclusive host access |
| Native continuations and builtin operator invocation (`function.c`) | Each invocation owns an aggregate for the popped callable, or for receiver and arguments, respectively | Register a distinct aggregate per invocation around callback; unregister before release. Other live VM operands are on contexts/stacks; there is no general scan of C frames | Model lock and existing synchronous invocation contract |
| Native ABI descriptors and per-call state (`module_native.c`, `native_services.c`) | Descriptor retains independently loaded code; host owns per-invocation ABI resources | ABI scalars and memory-service blocks are not traced Goat objects. Host cleans up on return, failure or fallback; any Goat object across a possible GC needs an ordinary traced root | No generic callback or allocator safe point |
| Explicit host roots (`gc.c`, `launcher.c`) | Registration is non-owning; caller owns a separate ref (e.g. REPL session scope) | Mark registered pointers; unregister before releasing. Process teardown discards the table | Execution lock or exclusive host access |
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
neither is an accessible live value. Walking an object graph and dropping a deep
last-reference chain still use recursion in this stage (#545, #546).

Mark begins from the roots above and delegates edge traversal to object types.
Sweep iterates the process's live list, clears marks on survivors and discards
unmarked objects with type-specific callbacks. Objects being swept must not re-enter
collection. GC-02 (#544) owns the correction for live targets of swept edges;
the current sweep is not a guarantee that all survivor refcounts were adjusted.
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
