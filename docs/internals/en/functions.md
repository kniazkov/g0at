# Function proofs: signatures, recursion and effects

[Contents](index.md) · [Русский](../ru/functions.md)

## A summary is not a memoized result

`function_summary_t` separates return type, effects, analysis status and C support.
ANALYZED can still mean TOP or UNKNOWN. A specialization is keyed by normalized
parameter types: constants/ranges lose payloads, known callables become FUNCTION,
and typed arrays become ARRAY. Captured values are not part of that key, so a
summary must never be reused as a cached result for a particular closure.
Missing arguments use the null domain; extra arguments are not parameter slots,
but their evaluation and effects cannot be discarded.

## Calls and recursion

build_function_call_graph() builds a bounded may-call graph of observed type
specializations. Unknown/imported/limited targets are explicit. Strongly connected
components group mutually recursive vertices; membership is not a termination proof.
The current analyze() supplies a graph limit of 1024 and solver limit of 64 rounds.
These are implementation budgets, not language limits or user-visible guarantees.

analyze_function_return_types() uses generic parameters and unknown captures in
isolated state. solve_function_recursion() refines recursive groups; exhaustion
leaves TOP/inconclusive. This is separate from ordinary value interpretation:

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/call_recursive.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/call_recursive.expect)

<!--! \include{lineno} call_recursive.goat -->
<!--! \verbinclude call_recursive.expect -->

Although a reader can evaluate this small call to zero, its ordinary write event
is TOP. Do not promise exact recursive evaluation because another pass can infer
some generic return types.

## Effects deliberately include more syntax

analyze_function_direct_effects() scans bodies, including syntactically present
dead branches, while nested bodies are handled separately. Captures distinguish
read and write access by declaration identity and name. propagate_function_effects()
then propagates possible effects through calls to a fixed point. Missing coverage
or UNKNOWN blocks purity. No I/O or ambient mutable access does not prove that a
function terminates or cannot throw.

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/effects_syntactic.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/effects_syntactic.expect)

<!--! \include{lineno} effects_syntactic.goat -->
<!--! \verbinclude effects_syntactic.expect -->

The external-write effect remains even though one assignment is behind false and
another follows return. This is the current conservative body contract, not a
claim that either assignment executes. A reachability test and an effects test
answer different questions.

## C support is another proof

Expression proofs, function contracts and body proofs live in `c_expression`,
`c_contract` and `c_body`. analyze() establishes generic receiver domains before
propagating effects, then checks contracts and bodies. It is not sufficient to
know the return type or mark the body pure.

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/c_body_unknown_calls.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/c_body_unknown_calls.expect)

<!--! \include{lineno} c_body_unknown_calls.goat -->
<!--! \verbinclude c_body_unknown_calls.expect -->

Calling through a mutable binding or generic function parameter is unsupported
here; the recognized `sqrt` call is supported. The test checks the C-support event,
not whether a host compiler happens to accept some generated text. Actual emission,
loading and invocation need the [native](native.md) tests as well.

## Internal init snapshots (#530)

`model/init_snapshot.h` prepares constructor selection without invoking functions.
It is not connected to `->`: automatic constructor execution belongs to #531.
No syntax, ordinary method access, or visible object creation behavior changes here.

`collect_init_snapshot(process, receiver)` excludes the receiver itself and follows
only `get_inheritance_prototypes`, never lexical enclosing scopes. It uses iterative
right-to-left postorder traversal, the reverse of property lookup on the real
inheritance graph: bases before derived objects and independent `[A, B]` as `B, A`.
Object identity is deduplicated, including diamonds and repeated direct prototypes.
Only the selected own `init` field of each ancestor is read. Missing fields are
skipped; a function stored on two different ancestors appears twice.

Selection ignores the field's access modifier but grants no new rights to the
function body. A present non-function (including null or an object with a `()`
handler) returns an owned InvalidOperation exception and no snapshot. Collection
never invokes a function, even if an earlier candidate is valid. A successful
result therefore represents the entire validated chain before any future calls.

The opaque snapshot owns a private read-only array containing the receiver and the
concrete selected function objects. It registers that array as a process GC root;
reference counts alone would not protect C-held values from tracing collection.
Changes to ancestor fields cannot change an existing snapshot. Function closures
and their original access metadata stay reachable through the selected functions.
A later collection sees the new fields. Each live snapshot has its own root.

Call under the process model lock with a live receiver belonging to that process.
Collection has no user callbacks or GC safepoints. Borrowed receiver/function
accessors are valid until `destroy_init_snapshot`; destroy the snapshot before its
process. On failure, release the returned exception with `clear_exception`.
Destruction unregisters the root and releases all references, without executing
constructors. The collector uses an explicit traversal stack rather than C recursion.

`component.init_snapshot.*` tests independent branches, chains, diamonds, repeated
prototypes, shared functions, lexical exclusion, own fields, all access modifiers,
non-function rejection without calls, immutable selection, concurrent snapshots,
closure/receiver survival across GC and release, and deep inheritance. They run in
Debug and Release CI via the `init-snapshot` label.
