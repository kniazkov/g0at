# Operators across module boundaries

[Русский](module-operators.ru.md)

Operator calls and escaped `arguments` use ordinary function, object and module
ownership. The object receiving an operation and the function implementing it
may belong to different modules. Selecting an operator never changes the
function's declaring module/file, captured lexical environment or access grants.
The same rule applies to a copied or inherited method.

## Access and failures

The caller must be allowed to read the operator property. Once selected, the
handler receives the original object as `this`, with the same private/protected
rights that an ordinary function would have with that receiver. Storing an
unrelated function in an operator slot grants no private rights.

Ordinary assignment creates a property with default file visibility. Exporting
an object does not make those properties external. A module can export a wrapper
that applies the operator in its declaring file; a foreign caller's direct
operation still fails if the slot is inaccessible. No new declaration syntax or
special operator visibility is introduced.

A handler from a failed declaring file raises that file's cached exception and
trace. Failure of the receiver's file or the dynamic caller's file does not
replace the declaring function's provenance. Repeated initialization of a failed
file does not repeat its effects. Exceptions raised inside a foreign handler keep
both the handler's included-file location and its caller chain.

## Analysis and `arguments`

`arguments` is known to be an array. Its length and elements are not inferred from
formal parameters or sampled calls: missing and extra actual arguments remain
observable, and reassignment of a parameter does not alter the saved array. An
escaped array keeps its values alive under ordinary GC rules.

Module interfaces preserve the array result and the original `functionRef` when
a function is re-exported. Scalar summaries apply only to compatible proven
argument domains. A custom or unknown receiver may invoke arbitrary code, so its
unknown effects invalidate affected assumptions. A proven invalid operation is
still a catchable runtime exception, including after serialization.

Typed native scalar entry points do not acquire an object/array ABI. Existing
[operator continuations](native-operators.md) keep ordinary function frames and
can return arbitrary runtime values, including saved argument arrays. The
separate pure scalar resource-retry policy does not replay an entered effectful
operator continuation.

## Immutable bytecode validation

The common image decoder validates operand requirements for counts, calls,
indexing, duplication and rotation. Control-flow joins must agree on operand
height and scope. Loop and finally transfers restore their saved scope heights;
logical selection regions retain their receiver/function slots until the matching
completion. A branch cannot enter another expression's selection, cross a
function's executable ownership boundary or fall through into a different body.

The compiler retains a NIL placeholder for an expression proven unreachable
inside a retained expression. It is never executed, but keeps both bytecode edges
well shaped for value-independent validation. Unreachable statements remain
omitted; earlier effects and runtime exceptions are unchanged.

The reader, module consistency checks and writer all use the common validation.
Module snapshots retain the resulting ownership and location metadata. Validation
never patches executable bytes. Opcode numbers, metadata fields and format
versions are unchanged. There are no migrations or old-bytecode compatibility
paths; malformed historical test constructions are corrected rather than accepted.

## Verification

The C/CTest cases cover exported interfaces, different receiver/handler owners,
private lexical access, saved arguments across GC, failed-file provenance and
cached initializer exceptions. Decoder/writer mutation tests exercise valid
zero/multiple indices and reject malformed counts, stack joins, saved logical
selections and control transfers.

`module.operators.*` builds optimized and unoptimized modules, compares source
execution with fresh processes reading only `.gbin` archives, and checks VM,
native-auto and native-required results. The saved consumers have no sources and
an isolated PATH with an unavailable selected compiler. Exact native entry counts
and unchanged archive bytes are checked separately from output and trace equality.
Repeated bytecode-only builds are byte-for-byte deterministic.

The PR records actual test results and platform limitations. Registering a test,
selecting native mode or checking this implementation's checklist entry does not
by itself mean CI or the post-merge master check has passed.
