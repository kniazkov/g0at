# Logical operator overloads

[Русская версия](operator-logical.ru.md)

For user objects, `a && b` and `a || b` look up the corresponding property on
`a` and its prototypes **before evaluating b**. The original receiver and selected
function are retained. If a function is found, b always runs, then the saved
function receives `this = a` and one argument, b. It must return an actual boolean;
any other result raises `INVALID_OPERATION`. Replacing the property or changing
the variable a during b does not change this selection.

A missing property uses ordinary truthiness and short-circuit evaluation. A
present non-function (including null) raises `INVALID_OPERATION` before b runs;
access failures and failed-file lookup errors also propagate before b. No lookup
on b or search past a shadowing property occurs. Handler exceptions preserve
their value and trace, and effects already performed are not rolled back.

```goat
var a = {};
a["||"] = func(other) { return other == 7; };
var result = a || 7; // calls the function even though a is truthy
```

`!!a` remains the separate built-in truthiness conversion. It neither calls a
logical handler nor applies overloaded `!` twice. `!(!a)` remains two negations.

## VM contract and lifetime

No opcode, executable-code rewriting, format version or migration was added.
`LAND` / `LOR` consume the left value and either short-circuit to arg1 with a
boolean result, or push two owning stack entries before evaluating the RHS:
original receiver then selected function. Built-in fallthrough uses null/null.
The compiler emits `BOOL arg0=1` after the RHS. It consumes these two entries and
the RHS, then calls the saved function through ordinary operator dispatch with
the strict boolean result policy, or converts the built-in RHS. `BOOL arg0=0`
remains the independent single-value conversion; other arg0 values are invalid.

The existing operand stack supplies GC roots and reference ownership. Nested
logical expressions and recursive/RHS calls use independent stack entries.
Exception, return and loop unwinding discard entries through existing scope
boundaries. There is no side table or continuation pointer that can survive an
unwound expression. A handler's call frame retains receiver and arguments until
completion using the shared operator-call machinery.

Analysis retains possible RHS and handler effects for unknown/user operands and
returns a conservative boolean result. Built-in constant short-circuit paths can
still be simplified. Native handler integration is tracked separately in #503.

Regression coverage is in `operator_logical_testing` in the existing C/CTest
suite, including serialized bytecode execution, GC and exception cleanup.
