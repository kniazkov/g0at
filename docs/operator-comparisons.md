# Comparison and logical-not handlers in the VM

[Русский](operator-comparisons.ru.md)

Step [#497](https://github.com/kniazkov/g0at/issues/497) connects the existing
comparison and `LNOT` instructions to function-valued properties on user-defined
objects. No opcode or module-format change is required.

## Comparison selection

`==`, `!=`, `<`, `<=`, `>` and `>=` look up the property with the same spelling
on the left object, after evaluating both operands once, left to right. The
handler receives exactly one argument, the saved right operand (including
`null`), and the original left object as `this`, even for an inherited handler.
There is no right-hand dispatch or implicit conversion.

For equality only, if the requested property is absent from the object and its
prototypes, the opposite property is looked up: `==` may call `!=`, and `!=` may
call `==`. The opposite handler's boolean result is negated. If both properties
are absent, equality uses object identity, and inequality its negation. A found
property containing `null` or another non-function is an `INVALID_OPERATION`,
not absence. An access error also stops selection and preserves the original
exception. Reaching a failed file-scope prototype also rethrows its saved
exception, even when no handler has been found. Neither case permits fallback. An explicit requested handler always
wins, even if both handlers return mutually inconsistent results.

Even `a == a` calls the selected handler: its result, side effects and exceptions
are observable. Identity is only the fallback when both properties are absent.
The four order handlers are independent: a missing `<`, `<=`, `>` or `>=` causes
`INVALID_OPERATION`; no order relation is inferred from another relation.

## Strict boolean results and continuations

Every comparison handler must return an actual boolean (`true` or `false`). A
number, string, object, `null`, or any other result causes `INVALID_OPERATION`;
truthiness conversion is never used. Opposite-equality fallback validates this
contract before negating the result.

The existing [operator continuation](operator-dispatch-api.md) validates both
synchronous and resumed Goat returns. Normal `finally` processing finishes
before validation; nested operations keep separate continuation state. Handler
exceptions and effects already performed are preserved. Lookup follows ordinary
prototype and access rules, and accepts functions only, not callable objects.

## Logical negation versus conversion

Explicit `!a` looks up `"!"` on a user object. The handler takes no arguments,
receives `a` as `this`, and must return an actual boolean. A missing property
keeps built-in logical negation; a non-function causes `INVALID_OPERATION`, and
access failures keep their usual exception.

`!!a` is a separate boolean-conversion token and emits `BOOL`. It always uses
built-in truthiness and does not call `"!"` or `"!!"`. `!(!a)` instead performs
two negations: the first may call the object's handler and the second negates
its boolean result. Conditions in `if`, `while` and other control flow also use
built-in truthiness independently of `"!"`. Unoverloaded `&&` and `||` retain
their built-in behavior; their own handler support belongs to #498.

## Built-in values, keys and verification

Built-in left operands keep their existing comparisons and negation, including
errors for incomparable order operands. Internal property-key lookup never
calls user equality: distinct objects remain distinct keys even if their `==`
handlers return true. Built-in model comparisons remain separate from VM
handler dispatch.

The `operator-comparison` C/CTest groups cover selection, access, strict result
contracts, effects, continuations, truthiness and key identity. Source programs
run before and after simplification, directly linked and after bytecode
serialization and fresh decoding. This validates VM dispatch, not native handler
integration, which belongs to #503. The conservative analysis safeguards introduced in #495 remain in force.

Language equality is therefore not an effect-free identity test for escaped
exception objects whose prototype chain includes a failed file. Use object-key
lookup when testing their identity without invoking operator lookup. The existing
repeated-failure include/import and thread fixtures use that identity oracle.
