# Exported interfaces and metadata-only analysis (#205)

`compile_module_sources_with_graph` compiles frozen sources with exact direct
bindings and a validated source dependency graph. The graph must describe the
same direct identities and hashes as the bindings; its synthetic root is not an
archive. `compile_module_sources_with_dependencies` remains available without a
transitive graph: unresolved foreign function references provide no callable
identity or summary. Neither API executes a dependency, loads its native library,
or reconstructs its AST. The existing graph verifier resolves `functionRef` only
within the exporting module's reachable dependencies, not unrelated process nodes.

The producer runs existing abstract interpretation and isolated function-summary
analysis without optimizer rewrites. It records every entry-file top-level
`external` declaration, including unknown/unreachable initializers, and its
mutability. Other files' and nested scopes' declarations are not this interface.
Mutable exports have no fixed type, value or function binding. Constants retain
only proven facts. Scalar encoding, validation, normalization and hashes use the
existing v1 implementation, including exact int64, Unicode strings with embedded
zeros and binary64 bit strings. No object or closure is serialized. Ambiguous NaN
payloads do not become exact scalar promises.

All own functions keep the existing source-preorder IDs across the module's DFS
file order, including nested/anonymous functions. Remaining bytecode FUNCs and
parameter names are checked against the function table. Entry export names and
mutability are cross-checked against bytecode with the shared declaration-scope
walker; nested or private declarations cannot impersonate an external binding. Aliases refer to the same
record. A proven foreign function is reexported with its true module/version/ID;
its summary is not copied, and transitive references do not add direct imports.
The code identity carries neither captured values nor a closure-instance identity.

Only completed isolated function proofs become `analysis.summaries`. General
parameter/result types and aggregate effects are exported; the producer does not
invent argument-alias, deep-independence, callback, exception or termination
relations absent from v1. A missing signature or fact remains unknown. An empty
complete effect set establishes only the analyzer's existing purity notion.

`analysis/module_interface.h` converts validated facts into the existing lattice.
A condition must cover **all** possible argument values, not merely overlap them.
Missing arguments are null; excess arguments still evaluate. Applicable results
are intersected, and complete effect bounds may be intersected. Unknown effects
retain conservative state invalidation. Exact facts which the lattice cannot
represent are weakened, never strengthened. Imported functions bypass AST-body
analysis and native specialization selection.

Compilation returns owned, source-positioned warnings for a statically proven
non-callable scalar used as a callee. These are not fatal: existing runtime error/exception behavior remains
unchanged. Opaque paths, including the current analyzer’s try/catch handling,
remain conservative rather than inventing a diagnostic. Unknown and mutable targets are not
rejected, and a summary-condition mismatch is not an illegal call signature.

Metadata does not replace runtime field lookup, access checks, initialization or
closure captures. Reads of known constants and calls of known functions retain
ordinary import order, side effects and exceptions. Type information alone does
not promise that a call terminates or that an object is deeply immutable.

The common JSON reader also rejects conflicting result facts in summaries with
provably overlapping argument conditions. Integer ranges, scalar values, numeric
unions and nullability participate; element constraints alone do not make array
conditions disjoint, because both admit an empty array. Effects are upper bounds,
not incompatible promises of actual effects. No-normal-return summaries do not
contradict conditional claims about a normal result. This is a structural contract
check, not reproof of a producer's implementation.

Verification uses `scripts/check_module_interface.py` with the existing
`module_import_execution_testing` harness, plus analyzer, JSON/canonical and
format-reference suites. Compilation is observed separately from execution;
archive-only dependencies with visible initializers verify that analysis does not
run their code. CLI build integration remains #191.

## Analytical-contract acceptance (#231)

The closed v1 schema cannot encode callback dependency, argument-result alias,
freshness or nested sharing. These relations remain unknown, not false. The
existing analyzer invalidates potentially affected state rather than inventing
independence. Const protects a binding, not the contents of its array. A complete
empty effect bound is not permission to remove a call or assume it cannot throw.

`check_module_interface.py` extends the existing cross-platform CI suite:

| Decision | Archive-only regression and metadata assertion |
| --- | --- |
| 86 | Replace `calculate`; results change from 4 to 6; mutable export has no fixed facts and consumer calls stay unknown. |
| 87 | Constant `next` retains captured reads/writes; consecutive results differ; neither producer nor consumer publishes a constant result. |
| 88 | A pure observed callback does not prove universal purity; consumer callbacks print, mutate captured state and throw; no stale scalar fact survives. |
| 89 | A pure identity can return its argument; mutation through either reference is visible through the other; no stale integer-element promise survives. |
| 90 | Clone has an independent outer array but shared inner arrays; bidirectional nested mutations remain visible without source files. |

Each compile invocation must produce no stdout. Dependency initializers have
visible output and source directories are removed before consumer compilation;
expected output is asserted only during explicit execution. Metadata assertions
exercise producer/consumer analysis separately from runtime checks. Existing
unit tests exercise precise effect bounds and conservative unknown-state behavior
directly, including const-held mutable arrays. No new wire-format or alternate
analysis/invalidation mechanism is introduced.
