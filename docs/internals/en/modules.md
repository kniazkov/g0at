# Modules: validate, freeze, initialize

[Contents](index.md) · [Русский](../ru/modules.md)

## Source construction is a transaction

`src/common/module_sources.*` resolves source files; imports and build configuration
are separate inputs. compile_module_sources_with_dependencies() borrows immutable
source snapshots and exact direct bindings. Every syntactic import, even in dead
code, must match; extra/transitive bindings are not an alternative search space.
Compilation creates bytecode, normalized metadata and file entries without executing
dependency code. The graph-aware variant can resolve transitive function references
from validated metadata, without analyzing dependency bodies.

The optional compilation backend runs after bytecode/metadata verification and
before source graphs are released. Failure aborts the transaction. Publication
belongs to the writer/build layer: producing bytes and atomically publishing an
artifact have different failure modes. Exported bindings must survive optimization.

## Validation is layered

| Layer | What it establishes | What it does not establish |
| --- | --- | --- |
| ZIP / container | Layout, declared contents, JSON, integrity hashes | VM semantics or executable dependencies |
| Bytecode | Wire structure and instruction constraints | Complete import graph |
| Consistency / module validation | Agreement between metadata, bytecode and declared native data | Successful runtime initialization |
| Dependency graph | Exact reachable identities and bindings | Success of user code |
| Native selection / loading | Compatible choice and validated runtime descriptors | General safety of arbitrary native code |

Start with `src/module/module_container.h`, `src/vm/module_validation.h`,
`module_graph.h` and `module_snapshot.h`. Diagnostic callbacks may receive partial
information for reporting; that never authorizes execution. “Not found”, malformed,
resource limit and allocation errors must remain distinct.

## Freeze before running

load_module_snapshot() validates the whole reachable graph before constructing
runtime scopes. The snapshot owns images, metadata, native payloads and one process.
Later execution does not reopen source archives. Names, versions and hashes fix
build identity; a physical path is not an access grant. Imports use consumer-local
direct bindings, not a global search through all loaded modules.

The image is immutable; caches, native dispatch and file states live outside it.
free_module_snapshot() requires all executions, threads and callbacks to have
stopped, then destroys runtime objects before borrowed image storage. An individual
module cannot be independently unloaded from a live snapshot.

## Initialization is persistent state

Each source file has a persistent scope and the states UNINITIALIZED, INITIALIZING,
INITIALIZED and FAILED. The executor enters through the once-only gate; repeated
includes/imports share the result or original exception. A file frame does not
inherit the caller's locals, receiver or access grants. Built-ins remain available.
The direct launch identity determines `__main__` and `__dir__` before execution.

A source file successfully parsed or an archive successfully validated has not yet
passed this runtime gate. An initializer can throw, and its failure must remain
observable by later consumers. Retrying source lookup would break snapshot identity.

## Inspecting and merging

The inspector reports stages independently, including skipped checks when their
prerequisites failed. Optional native probing is a bounded child-process operation,
not code execution inside the inspector. Merge combines compatible module contents
and information/native variants through their dedicated APIs; it must not obtain
answers by executing input constructors. See [tools](tools.md).

## Evidence and detailed contracts

Module scenario families cover source discovery, imports, validation, writer,
snapshot execution, lazy native loading, inspect and merge. The checked-in
`test/data/<family>/README.md` files map cases to contracts and negative controls.
Independent format helpers build malformed archives; using the production writer
as the sole expected-data generator could hide a shared bug.

[Sources](https://github.com/kniazkov/g0at/blob/master/docs/module-sources-api.md) ·
[Graph](https://github.com/kniazkov/g0at/blob/master/docs/module-graph-api.md) ·
[Loading](https://github.com/kniazkov/g0at/blob/master/docs/module-loading-api.md) ·
[Consistency](https://github.com/kniazkov/g0at/blob/master/docs/module-consistency-api.md) ·
[Writer](https://github.com/kniazkov/g0at/blob/master/docs/module-writer-api.md)
