# Appendix B. Language

[Contents](index.md) · [Русский](../ru/appendix-b-language.md) · [Previous](appendix-a-terminology.md) · [Next](appendix-c-vm-instructions.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-b-1"></a>

## B.1. Constructs and values

Values are `null`, booleans `true`/`false`, 64-bit integers, real `double` values, strings, functions, and user-defined objects. `var x;` creates a variable holding `null`; `var x = e;` uses an expression result. `const x = e;` requires an initializer. Chapter 7 covers implicit declarations and shadowing; `-w` enables warnings.

A function is written `func(a, b) { ... }`, a call `f(a, b)`, and a return `return e;` or `return;`. Without an explicit return, the result is `null`. A condition is `if (e) statement` with optional `else`. Exceptions use `throw e;` and `try statement catch (name) { ... }`. A block `{ ... }` is an expression returning its context object. Semicolons make boundaries explicit; a newline is not itself a statement-termination rule.

> [!CAUTION]
> There is no general syntax for arrays, `obj.name` / `obj[key]` access, `while` / `do/while` loops, `f()()` calls, or an import system. `Exceptions` is available as a value, but `Exceptions.INVALID_ARGUMENT` is not supported property access. Error names below denote ordinary strings actually produced by the VM.

`for (initial; condition; step) statement` executes initialization once and checks the condition before every iteration. A successful check is followed by the body and step. Initialization accepts one expression or a `var`/`const` declaration, including multiple declarators; condition and step each accept one expression. Every slot is optional, but both semicolons are required. An absent condition is true; `;` is an empty body. Header declarations have loop scope, and the body has a fresh nested scope per iteration. `return` and exceptions skip the remaining body and step. See [chapter 2](02-implemented-language.md) for examples.

> [!CAUTION]
> `break`, `continue`, and comma-expression sequences are not implemented. These words do not provide loop-control operations.

<a id="section-b-2"></a>

## B.2. Precedence

The table goes from strongest binding to weakest. It describes grouping, not side-effect order.

| Level | Operators | Grouping |
| --- | --- | --- |
| 1 | `name(...)`, `(expression)` | Named call; explicit grouping |
| 2 | `x++`, `x--`; `++x`, `--x` | Postfix, then prefix updates |
| 3 | `**` | Right associative; prefix qualification below |
| 4 | `+x`, `-x`, `!x`, `!!x`, `~x` | Prefix chain from right to left |
| 5 | `*`, `/`, `%` | Left associative |
| 6 | `+`, `-` | Left associative |
| 7 | `<<`, `>>` | Left associative |
| 8 | `<`, `<=`, `>`, `>=` | Left associative |
| 9 | `==`, `!=` | Left associative |
| 10 | `&` | Left associative |
| 11 | `^` | Left associative |
| 12 | `\|` | Left associative |
| 13 | `&&` | Left associative; short circuit |
| 14 | `\|\|` | Left associative; short circuit |
| 15 | `=` | Right associative |

Power needs qualification: `-2 ** 2` means `-(2 ** 2)`, but `2 ** -2` is also valid. `2 ** 3 ** 2` means `2 ** (3 ** 2)`. Actual pass order and prefix handling are in [parser.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/parser/parser.c) and [parsing_unary_operations.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/parser/parsing_unary_operations.c).

<a id="section-b-3"></a>

## B.3. Evaluation order

Ordinary binary operands are evaluated left to right. `&&` skips the right operand when the left is false; `||` skips it when the left is true. Both return booleans. A call evaluates arguments right to left, then loads the called function. Assignment evaluates its right side and returns the assigned value. Prefix update returns the new value; postfix update returns the old one.

Missing user-function parameters receive `null`; extra arguments are evaluated and discarded. Built-ins instead require their minimum argument counts. An exception interrupts normal evaluation: operands not yet started are not executed.

<a id="section-b-4"></a>

## B.4. Numeric and string operations

Integer `+`, `-`, `*`, unary minus, and updates wrap modulo \(2^{64}\). Mixed arithmetic produces a real. Division of two integers returns an integer for an exact representable quotient, otherwise a real: `3 / 2` produces `1.5`. This principle of least surprise preserves the fractional part. The special case `INT64_MIN / -1` produces real \(2^{63}\).

`%` accepts integers; a nonzero remainder follows the dividend's sign. `INT64_MIN % -1` is 0. A zero divisor in `/` or `%` produces `DIVISION_BY_ZERO`. `**` uses real `pow` and returns a real; special math-library results are not automatically converted into exceptions.

`~`, `&`, `|`, `^`, `<<`, and `>>` operate on 64-bit integers. Shifts accept counts from 0 to 63; right shift extends the sign. An unsuitable right operand of a numeric operation produces `INVALID_ARGUMENT`; an operation unsupported by the left type usually produces `INVALID_OPERATION`. Operand order can therefore matter even for error selection.

When the left operand of `+` is a string, the right value's text representation is appended. Ordinary arithmetic does not automatically parse numeric strings. Explicit `int` conversion is described in [appendix D](appendix-d-builtins.md).

<a id="section-b-5"></a>

## B.5. Comparisons and truthiness

Numbers compare by numeric value, including precise distinctions between large integers and reals. `NaN` is unequal to itself, its `!=` result is true, and ordered comparisons involving it are false. Strings compare lexicographically; booleans order `false` before `true`. Other objects use identity for equality; comparing different categories performs no arbitrary conversions. Unsupported ordering raises an error.

`null`, `false`, numeric zero, an empty string, and an empty user-defined object are false. Functions are true, and so is `NaN`. `!` negates truthiness; `!!` produces its boolean value.

<a id="section-b-6"></a>

## B.6. Error categories and sources

The model's ordinary exception strings are `DIVISION_BY_ZERO`, `IMMUTABLE_OBJECT`, `INVALID_ARGUMENT`, `INVALID_OPERATION`, `PROPERTY_ALREADY_EXISTS`, `PROPERTY_IS_CONSTANT`, and `PROPERTY_NOT_FOUND`. Having a string in the exception object does not mean each low-level status corresponds to an available source construct. Writing a constant produces `PROPERTY_IS_CONSTANT`; a user may throw any other value too.

Parse errors, native ABI failures, and the fatal empty-stack check are not these language exceptions. Chapter 5 describes lexical limitations; appendix G summarizes implementation boundaries.

Main sources: [integer.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/integer.c), [real.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/real.c), [string.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/string.c), [common_methods.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/common_methods.c), [bitwise.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/lib/bitwise.h), [exceptions.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/exceptions.c). Examples in chapters 2, 6, 9, and 12 demonstrate executable cases of these rules.
