# Bytecode, objects and execution

[Contents](index.md) · [Русский](../ru/execution.md)

## Generation ends at an immutable image

Graph nodes emit instructions through `code_builder_t` and constants through
`data_builder_t`. link_code_and_data() assembles them into a bytecode image.
`src/vm/bytecode.h` defines explicit fixed-width wire records; `image.c` validates
counts, offsets and instructions. These records are not an arbitrary dump of C
objects. decode_bytecode_image() copies untrusted bytes into aligned storage and
validates them before use.

The image stays immutable. Runtime native bindings, declaration identity and
caches live in the wrapper or process objects. borrow_bytecode_image() creates a
separate runtime wrapper without copying the image: its owner must retain that
image until the wrapper and all image-backed objects are gone. A module's file
entries share an image rather than duplicating it for each include.

## VM and model have different jobs

run() dispatches opcodes in `src/vm/vm.c`. The model in `src/model` supplies objects,
contexts, stacks, functions, properties, access identities and exceptions. A
`process_t` is the Goat runtime owner, not a child OS process handle from `lib/process`.
A context links execution frames and lexical data; a function retains the environment
needed by a closure. Structural AST parents are no longer involved at this stage.

`operation_result_t` owns a non-NULL normal value or thrown value. Goat null is a
valid object; C NULL is not a substitute for a thrown null. Preserve the exception
and any saved trace when crossing a call or module-initialization boundary.
A valid language exit terminates the OS process; run() does not return through
normal host cleanup for that path. Test it in a child process.

## References and collection cooperate

INCREF/DECREF account for owned runtime references. Type implementations may pool
cleared shells, and static singletons need not have ordinary mutable counters.
Cycles require reachability collection in addition to reference counting.
collect_garbage() marks module file roots, launch-directory and string caches,
explicit host roots, each pending worker's callable, context chain, pending
values, exceptions and operand stack, then sweeps the process object list.

A local C pointer alone is not a GC root. add_gc_root() registers a tracing
root but does not acquire an owned reference. Duplicate registration is set-like,
not counted. Native descriptors independently keep OS-loaded code alive.

See the [ownership and tracing audit](memory-management.md) for the root table, collection boundaries, and limitations.

## Threads and initialization

Program threads belong to the same runtime process. `execution_mutex` protects
short model operations; blocking waits must not retain it. A separate recursive
initializer lock serializes module initialization. join_program_thread() rejects
self/cyclic waits and waits during dynamic initialization. An OS thread API alone
does not establish these language-level contracts.

run_module_file() is an outer entry for prepared files, not a reentrant call during
an active run. Initialization records success or the original failure in the shared
file state. Includes/imports reuse that state rather than rerunning arbitrary
source text; see [modules](modules.md).

## Test the property at its own level

`core.unit` checks VM/model contracts, including ownership and exceptions;
`core.functional` checks language-visible behavior. Module execution scenarios
check persistent scopes and once-only initialization. Native invocation tests must
exercise both the selected native path and fallback. Output equality alone cannot
prove GC reclamation, image immutability, or the absence of a second initialization.
