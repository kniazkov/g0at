# Operator dispatch foundation

[Русский](operator-dispatch-api.ru.md)

This is the runtime foundation for checklist step [#494](https://github.com/kniazkov/g0at/issues/494).
The foundation itself does not route opcodes. [Step #496](operator-arithmetic.md)
connects arithmetic, bitwise and unary update instructions to it.
[Step #497](operator-comparisons.md) connects comparisons and logical negation.
Binary logical, index and object-call integrations remain later checklist steps. The
bytecode instruction set and format remain unchanged.

## Lookup and ownership

`lookup_operator_handler(access, receiver, key)` considers only user-defined
objects. It follows ordinary accessible property lookup, including prototype
order and private-slot rules, using the rights of the expression's context.
It never invokes user indexing or tests general callability.

The result distinguishes an inapplicable built-in receiver, an absent property,
a selected function, and an error. A found `null`, number or callable-looking
object is an `INVALID_OPERATION`, not absence; it shadows inherited handlers.
Access denial, pending initialization and failed-file errors are preserved.
Only actual Goat functions, closures and built-in functions qualify.

A successful result retains the selected function. Replacing the property after
selection cannot invalidate that selection. An error owns its exception value
and optional origin trace. Release either result with `clear_operator_lookup`.
Lookup does not retain the receiver or evaluate any argument.

## Receiver and lexical rights

`call_operator_handler` takes the selected function and original receiver, even
when lookup found the function in a prototype. Arguments are already on the VM
operand stack, in ordinary call order, and follow ordinary call ownership.
The receiver is not an element of `arguments`.

The operator handler's call frame binds bare `this` to the original receiver.
This binding is separate from activation storage and lexical access rights:
parameters and locals do not become receiver properties, and installing a
function on an object does not grant unrelated private access.

Ordinary `CALL` and `CALLM` keep their current scope-local `this` semantics.
The operator binding belongs only to the handler's call frame. A nested block or
object still has its own scope-local `this`, and a nested function gets its own
call scope. To use the original receiver there or in an escaping closure, save
`var self = this` in the handler first. The receiver is retained and marked while
the handler frame is live; a captured ordinary variable follows normal lifetime
rules.

## Completion and unwinding

The call API accepts an explicit resume instruction and one result policy:

- `OPERATOR_RESULT_VALUE`: leave the returned value on the operand stack.
- `OPERATOR_RESULT_BOOL`: require an actual boolean, without coercion.
- `OPERATOR_RESULT_NEGATED_BOOL`: require a boolean and negate it.
- `OPERATOR_RESULT_DISCARD`: release and remove the returned value.

A built-in can complete synchronously. A Goat function can transfer execution to
its body; successful entry is not successful completion. Both paths use the same
result policy. Deferred state belongs to the particular callee frame, never a
single mutable slot on the thread or caller.

Normal return first executes intervening `finally` bodies, including any nested
calls or replacement return. Only after the callee frame has been removed is its
final result checked or discarded. A bad boolean result raises
`INVALID_OPERATION` at the originating operator call site, where the caller's
catch/finally can handle it; the completed handler cannot catch that violation.
The explicit success-resume address does not replace the error's origin.

A throw uses ordinary exception unwinding and preserves its value and trace.
Destroying the callee drops its continuation state without applying a normal
result policy. Terminal native/backend failures remain distinct from catchable
language exceptions. Operator Goat handlers stay on the VM path until the native dispatch step
[#503](https://github.com/kniazkov/g0at/issues/503), including when the receiver
is also the function closure. Ordinary call specialization is unchanged. No
executable bytecode is rewritten.

## Verification

The component tests enter this API directly and execute real compiled Goat
bodies through the VM. They establish lookup, receiver, result-policy and unwind
contracts without prematurely enabling source-language operator dispatch. The
existing ordinary-method and access suites remain regression coverage.
