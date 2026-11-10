# Actual arguments in native calls

The VM and native call boundaries preserve the original argument count and order.
The receiver `this` is not an argument. Missing formal parameters and explicitly
supplied `null` remain distinct: only the latter occupies an actual argument slot.
Specialization guards and purity proofs still use the formal-parameter projection,
with missing formals treated as `null`; this does not pad the vector passed to an
adapter. Module format v1 and its scalar signature schema are unchanged.

## Typed native support

A proven typed native function can read `arguments.length`, including through
parentheses. This reads its own call's actual count, independent of parameter
assignment. Generated function declarations, adapters and direct native calls pass
the count and actual-value vector separately from typed formal parameters. Direct
calls evaluate each actual argument once, from left to right, before entering the
callee. Additional representable arguments are retained in the vector.

The legacy native boundary accepts scalar/string actual values, including `null`
in extra positions. The module boundary uses its existing scalar/string ABI and
bridges a module null to the corresponding internal native null representation.
An unsupported value, including an ordinary object in an extra position, causes
fallback before adapter entry, without consuming or reevaluating arguments.
Typed formal parameters must still match an available specialization; this work
does not add a general boxed native parameter or return type.

A literal nonnegative index, such as `arguments[0]`, can also be lowered when
the slot is within the formal parameter list and its original scalar/string type
is proven. The read uses the original actual vector, not the possibly reassigned
parameter. Dynamic indices and unproved or extra-position element types are not
covered by this typed path.

Each captured direct-call operand consumes one temporary-budget slot. The existing
128-slot generated-frame limit therefore also bounds the total number of actual
vector elements across call sites; vectors do not bypass the entry stack guard
with an unbounded automatic array.

## Shared runtime and remaining boundaries

Native support for `arguments.length` does not make every use of `arguments` a
native expression. Materializing or returning the immutable array, unproved indexed
reads, mutation errors, escaped arrays and capturing an outer array in a closure
continue to use the existing VM runtime when the typed lowering cannot represent
them. Those arrays retain their original values, are shallowly immutable, and
remain alive while referenced. Nested calls have their own arguments; accessing
an outer snapshot requires saving it in an ordinary variable.

Native success must be measured for the relevant function. A successful native
sibling, or merely selecting `--native auto` or `--native required`, does not prove
that an arguments-using function entered native code. Post-entry failures retain
the existing no-replay contract, apart from independently proven pure retry.

## Focused checks

`integration.module_dispatch.actual-vector.auto` and `.required` use a controlled
adapter to assert exact counts of zero, one and four, explicit null, and ordered
integer/string/boolean extras. The missing-null cases verify formal guard matching;
extra-object cases verify zero adapter calls and zero resolver calls. These checks
exercise the production module boundary without claiming generated-body coverage.

`native.index` also isolates arguments-owning generated functions, with ten exact
native entries and no retry in each scalar and persisted-module fixture. The
scalar fixture checks all five typed values after parameter reassignment and
nested direct calls. The module fixture includes escaped arrays/closures, GC
pressure, mutation rejection and extra-argument effect ordering. Both remove
sources and make the compiler unavailable before persisted execution. VM checks
cover `--optimize none` and `all`; native execution requires `all` by the existing
CLI contract. `native.native_call` checks malformed extra payloads at a generated
adapter, and `core.analysis` verifies conservative unsupported signatures.
