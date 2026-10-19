# Appendix B. Language

[Contents](index.md) · [Русский](../ru/appendix-b-language.md) · [Previous](appendix-a-terminology.md) · [Next](appendix-c-vm-instructions.md)

Revision 4. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-b-1"></a>

## B.1. Constructs and values

Values are `null`, booleans `true`/`false`, 64-bit integers, real `double` values, strings, functions, and user-defined objects. `var x;` creates a variable holding `null`; `var x = e;` uses an expression result. `const x = e;` requires an initializer. Chapter 7 covers implicit declarations and shadowing; `-w` enables warnings.

A function is written `func(a, b) { ... }`, a call `f(a, b)`, and a return `return e;` or `return;`. Without an explicit return, the result is `null`. A condition is `if (e) statement` with optional `else`. Exceptions use `throw e;` and `try statement catch (name) { ... }`. A block `{ ... }` is an expression returning its context object. Semicolons make boundaries explicit; a newline is not itself a statement-termination rule.

> [!CAUTION]
> `while` / `do/while` loops and imports are not implemented. `obj.name`, `obj[key]`, `Exceptions.INVALID_ARGUMENT` and call chains such as `f()()` are supported.

`for (initial; condition; step) statement` executes initialization once and checks the condition before every iteration. A successful check is followed by the body and step. Initialization accepts one expression or a `var`/`const` declaration, including multiple declarators; condition and step each accept one expression. Every slot is optional, but both semicolons are required. An absent condition is true; `;` is an empty body. Header declarations have loop scope, and the body has a fresh nested scope per iteration. `return` and exceptions skip the remaining body and step. See [chapter 2](02-implemented-language.md) for examples.

> [!CAUTION]
> `break`, `continue`, and comma-expression sequences are not implemented. These words do not provide loop-control operations.

<a id="section-b-2"></a>

## B.2. Precedence

The table goes from strongest binding to weakest. It describes grouping, not side-effect order.

| Level | Operators | Grouping |
| --- | --- | --- |
| 1 | `expr(...)`, `expr.name`, `expr[indices]`, `(expression)` | Left-to-right postfix chains; explicit grouping |
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

Power needs qualification: `-2 ** 2` means `-(2 ** 2)`, but `2 ** -2` is also valid. `2 ** 3 ** 2` means `2 ** (3 ** 2)`. Actual pass order and prefix handling are in [parser.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/parser/parser.c) and [parsing_unary_operations.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/parser/parsing_unary_operations.c).

<a id="section-b-3"></a>

## B.3. Evaluation order

Ordinary binary operands are evaluated left to right. `&&` skips the right operand when the left is false; `||` skips it when the left is true. Both return booleans. A call evaluates arguments left to right, then loads the called function. Assignment evaluates its right side and returns the assigned value. Prefix update returns the new value; postfix update returns the old one.

Missing user-function parameters receive `null`; extra arguments are evaluated and discarded. Built-ins instead require their minimum argument counts. An exception interrupts normal evaluation: operands not yet started are not executed.

<a id="section-b-4"></a>

## B.4. Numeric and string operations

Integer `+`, `-`, `*`, unary minus, `++` and `--` saturate to the full range from `-9223372036854775808` to `9223372036854775807`. Saturation occurs after each operation in evaluation order. `-INT64_MIN`, `abs(INT64_MIN)` and `INT64_MIN / -1` return integer `INT64_MAX`. Increment at the maximum and decrement at the minimum leave the value unchanged. Mixed and real arithmetic are unchanged. Division of two integers returns an integer for an exact quotient, subject to the saturation case above, otherwise a real: `3 / 2` produces `1.5`. This principle of least surprise preserves the fractional part. Integer literals outside the range are compilation errors; `-9223372036854775808` is valid. Bitwise operations, including shifts, do not saturate.

Saturation is not associative: `(MAX + 1) - 1` gives `MAX - 1`, while `MAX + (1 - 1)` gives `MAX`. A loop counter incremented at `MAX` remains there; a loop must not rely on overflow to terminate.

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

Main sources: [integer.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/integer.c), [real.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/real.c), [string.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/string.c), [common_methods.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/common_methods.c), [bitwise.h](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/lib/bitwise.h), [exceptions.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/exceptions.c). Examples in chapters 2, 6, 9, and 12 demonstrate executable cases of these rules.

<a id="section-b-index"></a>

## B.5. Indexed access and the current object

```goat
var obj = { this["x"] = 1; this[3] = 2; this[true] = 3; };
println(obj.x);       // 1
println(obj[3]);      // 2
println(obj[true]);   // 3
obj["+"] = 4;
```

The property dictionary maps **objects to objects**. Keys are not converted to strings: `3`, `"3"`, `true` and `'3'` are different. Numerically equal integer and real keys (`3` and `3.0`) name the same entry. Strings and characters compare by value; user objects and functions by identity. A key may be `null` or the receiver itself.

`obj["x"]` shares properties and prototype rules with `obj.x`. Missing properties read as `null`. Assignment updates the first matching mutable property on the object or its prototypes, otherwise creating an own property. Constant writes raise `PROPERTY_IS_CONSTANT`; immutable receivers raise `IMMUTABLE_OBJECT`.

Reads evaluate the receiver, then indices left to right. Assignments evaluate **the right-hand side first**, then the receiver and indices, each exactly once, and return the assigned value. `a[k] = b[j] = value` associates right to left. `obj[key](args)` passes a receiver like `obj.method(args)`; call arguments are evaluated before the receiver. `++` and `--` still accept only variables.

Syntax allows `obj[]` and `obj[i, j]`, but ordinary objects accept **exactly one index**. Other counts raise `INVALID_ARGUMENT` after operand evaluation. This prepares for future multi-index types; arrays also accept one index, but dispatch to their own access methods. Strings currently use property indexing too: `s["length"]` reads the length, while `s[0]` does not replace `s.charAt(0)`.

`this` denotes the object of the current scope, including `{ ... }`. **It is not the method receiver as Java/JS experience might suggest.** See [Section 10.8](10-contexts-and-closures.md#section-10-8) for the comparison and example.

Indexing executes in the VM through dedicated `GETINDEX`/`SETINDEX` instructions and `get_index`/`set_index` virtual methods accepting an index list. Native lowering of `[]` is not implemented yet. All `index_*` fixtures are automatically checked at both optimization levels, after source regeneration and after saved-bytecode loading.


## Arrays

An array is a separate mutable object type. Literals use square brackets;
a trailing comma is allowed and does not add an element:

```goat
var values = [1, "two", [true, false,],]
println(values.length) // 3
values[0] = 42
println(values[2][1]) // false
```

Elements are evaluated once, from left to right. Every literal creates a fresh
array, including `[]`. Elements may hold any values, nested arrays, functions
and object references. Holes such as `[1,,2]` are not allowed.

Length is fixed in this version. `array.length` is a read-only integer property.
Indexed reads and writes require exactly one **integer** index in the range
zero through `length - 1`. Negative/out-of-bounds indices, real numbers (even
`0.0`), other types or other index counts raise `INVALID_ARGUMENT`. Assignment
does not grow an array. Named properties cannot be added; `a["length"]` does
not replace `a.length`. Trailing commas are allowed in literals, not indices.

Assignment shares the array reference, so aliases observe mutations. Equality
compares identity rather than contents. Empty arrays are false; nonempty arrays
are true. Printing uses `[1,"text",true]`; cyclic array references become `[...]`
and cyclic dictionary references become `{...}`. Garbage collection traverses
elements and reclaims unreachable cycles, including mixed array/object cycles.

Arrays execute only in the VM for now, with no native representation or ABI.
Automatic mode keeps this work in the VM. Newlines do not terminate expressions:
use a semicolon between separate expressions `a; [1]`, since `a[1]` is indexing.

Arrays inherit from the built-in `Array` prototype, which inherits from `Object`.
Both `[].instanceOf(Array)` and `[].instanceOf(Object)` return `true`.
An empty array prints as `[ ]`, including inside other objects and arrays.

## Object.clone()

`obj.clone()` dispatches to the receiver's virtual `clone` method. It is inherited
from `Object` and can be overridden by an ordinary property, just like `instanceOf`.
`obj["clone"]()` works too. A detached method (`var copy = Object.clone; copy(obj)`)
accepts the object as its first argument; a call without an object raises
`INVALID_ARGUMENT`.

Arrays and ordinary objects are shallow-copied: the outer container is new, while
nested values remain shared. Prototypes and property constness are preserved.
Direct self-references in values are retargeted to the copy. Immutable values and
functions may be returned without allocating another object, retaining a reference.
