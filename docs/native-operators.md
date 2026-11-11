# Native operator continuations

[Русский](native-operators.ru.md)

User-defined operators use the same selection, access checks, receiver, actual
arguments and result contracts in VM and mixed native execution. Numeric/string
specializations retain their existing typed C implementation. Operator-bearing
functions that cannot use that implementation may instead have a compiled
continuation.

## What executes natively

A continuation is generated C with a dispatch case for each original instruction
in one function body. The compiled driver selects the next case and calls a host
service for one original instruction. The host uses the existing operator/runtime
implementation. If that instruction calls a VM handler, the service executes the
nested call until control returns to the suspended function. A handler may itself
call an independently safe typed native helper. This is a runtime-assisted native
path, not a claim that object arithmetic has become unboxed arithmetic or that
these calls are faster than VM execution.

The driver never hands its complete function body to a VM fallback. Its original
instruction addresses refer to the unchanged bytecode image. A separate optional
`goat_native_continuation_query` export identifies these functions; they do not
pretend to have scalar signatures or purity guarantees. Existing scalar entry
layouts and bytecode version remain unchanged.

## Values, effects and exceptions

The ordinary function frame is created before entering a continuation. It owns
lexical access, the original receiver and all actual arguments, including extra
arguments and missing-formal null bindings. The VM stack and contexts retain
arbitrary object results and GC roots. Escaped arrays and closures use their
ordinary lifetimes.

Every runtime step uses the existing instruction semantics. In particular:

- Arithmetic, bitwise, unary and call handlers may return any value.
- Comparison, logical and negation handlers require a strict boolean result.
- Logical handler selection happens before the right operand and is retained
  across later mutation. Built-in short-circuit behavior is unchanged.
- Indexed writes discard the setter's return while preserving the assigned value.
- Compound assignments and prefix/postfix updates retain the target and evaluate
  receiver, indices and right-hand effects exactly once.
- Method/operator access checks and private/protected boundaries are preserved.

An exception leaving the native function stops at a distinct native boundary
before the suspended C invocation is released. The original exception and trace
then continue through the caller's normal VM unwinding. No entered continuation
is replayed after an exception, backend error or resource failure. The existing
pure typed-adapter retry policy is separate. Host cancellation retains its requested
status after frame cleanup; it is not reclassified as a native backend failure.
Language exit remains nonreturning process termination.

## Saved modules

The optional continuation export travels inside the normal native library in a
ZIP module. Loading validates image ownership, metadata, function identity and
original instruction ranges before binding. No source or compiler is needed to
execute the saved library. Native-off mode uses the same original VM bytecode.

A variant may contain zero scalar specializations when it contains genuine
continuations. Only the two scalar-specialization array minimum-length rules are
relaxed; schema fields and format versions are unchanged. Native-required mode
must still find actual usable native functions, not merely an empty array or an
unvalidated symbol. Continuation-bearing variants are loaded and validated during
preparation; scalar-only variants keep their existing lazy resolution.

## Verification

The focused component tests check real compiled C invocation, a VM-only handler
and a native helper, exact entry counts, arbitrary object identity, strict boolean
failure, finally blocks, recursive GC, terminal errors and unchanged bytecode.
Generated-driver tests separately verify dispatch, status propagation and invalid
PC rejection. The `native.operators.*` C/CTest scenarios compare all operator
families in unoptimized/optimized VM, native auto/required and source-less,
compiler-less persisted execution. A native flag alone is not evidence that a
function entered native code.

This change does not add opcodes, change precedence, overload ordinary assignment
or property-dot access, migrate old bytecode, or rewrite executable images. The
broader cross-module analysis/serialization matrix remains a separate task.
