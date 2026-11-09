# Arithmetic operator handlers in the VM

[Русский](operator-arithmetic.ru.md)

Checklist step [#496](https://github.com/kniazkov/g0at/issues/496) connects the
existing arithmetic and bitwise VM instructions to ordinary function-valued
properties on user-defined objects. The instruction set and module format do
not change.

## Properties and arguments

Binary `+`, `-`, `*`, `/`, `%`, `**`, `&`, `|`, `^`, `<<` and `>>` look up the
property with the same spelling on the left object. They pass exactly one
argument: the right value, including an explicit `null`. Unary `+`, `-` and `~`
look up the same spelling and pass no arguments. A shared `+` or `-` handler can
therefore distinguish the two forms using `arguments.length`. A missing formal
parameter is `null`, but that does not add an element to `arguments`.

`++` and `--` use their own properties, without arguments. They never fall back
to `+ 1` or `- 1`. The handler returns the new value. Existing surrounding update
code writes it back; prefix returns the new value and postfix preserves the old
value. The old object is not deep-copied. Single evaluation and early write
checks for complex assignment targets belong to step #500.

Every handler may return any value, including a string, object or number. Its
bare `this` is the original receiver, including when the property was inherited.
The receiver is not included in `arguments`. Nested scopes retain the ordinary
scope-local `this` rules explained in the [dispatch API](operator-dispatch-api.md).

## Evaluation and failures

Operands are evaluated once, from left to right. A binary instruction selects
the handler only after both operands have completed. Changing the saved left
object's property during the right operand affects the current operation.
Reassigning the variable from which the left operand came does not replace the
already saved receiver. No right-hand lookup or implicit conversion of a user
object to a number or string is introduced.

Lookup follows ordinary prototype order and access rules. An absent property,
`null`, or any other non-function causes `INVALID_OPERATION`. A callable user
object is not a function handler. Access and failed-module errors retain their
usual meaning. Goat functions, closures and built-in functions are accepted;
their own argument restrictions and exceptions are preserved. Effects completed
before an exception are not rolled back.

A Goat handler can suspend the operator while its body runs. Its final return
value resumes the instruction after the operator, after normal `finally`
processing. Nested operators have separate continuations. The shared dispatch
foundation retains the receiver and arguments through calls and collection.

## Scope and verification

Built-in left values keep their existing operations. Comparisons, logical
operators, indexing and object calls are separate checklist steps. This step
implements VM dispatch, not native handler integration (#503); running with
native execution enabled may still use the conservative VM fallback.

The 12 `operator-arithmetic` C/CTest component groups verify source expressions,
operator arities, receiver selection, failures and built-in regressions. They run
with assertions enabled in Debug and Release, alongside the existing dispatch
and analysis component groups.
