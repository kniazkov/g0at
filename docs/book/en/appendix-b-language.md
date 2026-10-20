# Appendix B. Language

[Contents](index.md) · [Русский](../ru/appendix-b-language.md) · [Previous](appendix-a-terminology.md) · [Next](appendix-c-vm-instructions.md)

Revision 4. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-b-1"></a>

## B.1. Constructs and values

Values are `null`, booleans `true`/`false`, 64-bit integers, real `double` values, strings, functions, and user-defined objects. `var x;` creates a variable holding `null`; `var x = e;` uses an expression result. `const x = e;` requires an initializer. Chapter 7 covers implicit declarations and shadowing; `-w` enables warnings.

A function is written `func(a, b) { ... }`, a call `f(a, b)`, and a return `return e;` or `return;`. Without an explicit return, the result is `null`. A condition is `if (e) statement` with optional `else`. Exceptions use `throw e;` and `try statement catch (name) { ... }`. A block `{ ... }` is an expression returning its context object. Semicolons make boundaries explicit; a newline is not itself a statement-termination rule.

> [!CAUTION]
> Imports are not implemented. `while` / `do/while`, `obj.name`, `obj[key]`, `Exceptions.INVALID_ARGUMENT` and call chains such as `f()()` are supported.

`for (initial; condition; step) statement` executes initialization once and checks the condition before every iteration. A successful check is followed by the body and step. Initialization accepts one expression or a `var`/`const` declaration, including multiple declarators; condition and step each accept one expression. Every slot is optional, but both semicolons are required. An absent condition is true; `;` is an empty body. Header declarations have loop scope, and the body has a fresh nested scope per iteration. `return` and exceptions skip the remaining body and step. See [chapter 2](02-implemented-language.md) for examples.

> [!CAUTION]
> Comma-expression sequences are not implemented. `break` and `continue` target the nearest loop and run intervening finally blocks.

Exception syntax also includes `try statement finally statement` and
`try statement catch (name) statement finally statement`. Every body accepts a
single statement or a block and has its own lexical scope. Cleanup runs once on
every ordinary exit; a transfer escaping cleanup replaces the pending transfer.
Return values are evaluated before cleanup and retained without copying. See
[chapter 12](12-exceptions.md) for nested cleanup and exception precedence.

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

Array methods can change length. `array.length` is a read-only integer property.
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

This section describes the current implementation. The agreed extension for access
modifiers, method adaptation, and capture transfer is in [appendix I](appendix-i-access-control.md).

`obj.clone()` dispatches to the receiver's virtual `clone` method. It is inherited
from `Object` and can be overridden by an ordinary property, just like `instanceOf`.
`obj["clone"]()` works too. A detached method (`var copy = Object.clone; copy(obj)`)
accepts the object as its first argument; a call without an object raises
`INVALID_ARGUMENT`.

Arrays and ordinary objects are shallow-copied: the outer container is new, while
nested values remain shared. Prototypes and property constness are preserved.
Direct self-references in values are retargeted to the copy. Immutable values and
functions may be returned without allocating another object, retaining a reference.

## Array methods

| Method | Behavior and return value |
|---|---|
| `push(...values)` | Append values; return the new length. |
| `prepend(...values)` | Prepend in argument order; return the new length. |
| `insert(index, ...values)` | Insert before an index in `0..length`; return the new length. Index `length` appends. |
| `pop()` | Remove and return the last value, or `null` if empty. |
| `remove(index)` | Remove and return the value at an index in `0..length-1`. |
| `clear()` | Remove all elements and release the buffer; return the array. |
| `indexOf(value)` / `lastIndexOf(value)` | First / last index equal under `==`, or `-1`. |
| `slice(start = 0, end = length)` | New array from `[start, end)`, with `0 <= start <= end <= length`. |
| `reverse()` | Reverse in place; return the array. |
| `sort()` | Stable in-place sort using `<`; return the array. |
| `toString()` | Array notation: `[1,"text"]`, or `[ ]` if empty. |
| `toString(separator)` | Join plain element strings without brackets; separator is a string or character. An empty array produces an empty string. |

Indices and boundaries are nonnegative integers. Invalid types, ranges or argument
counts raise `INVALID_ARGUMENT`. Empty `push()`, `prepend()` and `insert(index)`
are no-ops. Other methods require exactly the documented count, except for the
optional slice boundaries and separator.

Slices copy the element buffer rather than creating a view. Mutating or growing a
slice does not change the original buffer. Copies are shallow: nested objects stay
shared. Unlike `clone`, a slice does not retarget references to the original array.

Sorting compares values, not strings: mixed integers and reals compare numerically.
If `<` throws, the original order stays entirely unchanged. Equal elements retain
their order. Search propagates `==` exceptions too; arrays and ordinary objects
compare by identity.

Methods are inherited from `Array` and execute in the VM. A detached method accepts
the array first: `var append = Array.push; append(array, 1, 2)`.

## Conditional expressions

`condition ? when_true : when_false` evaluates the condition once using ordinary
Goat truthiness, then evaluates exactly one branch and returns its value. The other
branch has no effects and cannot throw. Both branches are mandatory and may return
different types; the chosen value is not converted to the other branch's type.

The operator binds less tightly than `||`, `&&` and `->`, and more tightly than an
outer assignment: `x = ready ? a : b` assigns the selected value to `x`.
Assignments are allowed in either branch: `ready ? x = a : y = b`.
Nesting associates to the right: `a ? b : c ? d : e` means `a ? b : (c ? d : e)`.
Use parentheses for a conditional condition, receiver or prototype:
`(a ? b : c) ? d : e`, `(ready ? left : right).value`, `(ready ? P : Q) -> {}`.
The conditional expression itself is not an assignment target.

The analyzer joins normal-result values and branch states, excluding paths that
throw or return. Native execution supports conditions with a supported scalar
representation and branches with the same supported result type (integer, real,
boolean, character or string). Heterogeneous results, including integer versus
real, currently use the VM. A statically proven, discardable condition permits
lowering just the selected branch. Native branch setup and calls remain lazy.

```goat
const absValue = func(x) { return x < 0 ? -x : x; };
println(absValue(-7));
println(true ? "ready" : "waiting");
```

## Collection iteration

`for (const item in collection) statement` and `for (item in collection) statement`
are equivalent. The binding is a fresh constant on each iteration, shadows any outer
name, and is captured separately by closures. `var`, multiple bindings, and assignment
targets such as `obj.x` are not allowed in the header. The collection expression is
evaluated exactly once, before introducing the binding.

Arrays are traversed by value in element order using a shallow copy made before the
loop. User-defined objects are traversed by their own keys in dictionary comparison
order, also copied before the loop. Changes to the original collection do not change
the iteration sequence; referenced objects remain shared. Inherited properties are
not included. Only these two types are iterable: strings and other values raise
`INVALID_OPERATION`. Provably unsupported sources, including scalar literals and
closed scalar expressions through constant bindings, are compilation errors even
with optimization disabled. Sources whose type is not proven are checked at runtime.

`break` and `continue` target the nearest loop. `return` and exceptions unwind its
contexts normally. Collection iteration currently executes only in the VM.

```goat
for (value in [10, 20, 30]) println(value);
const point = { x = 3; y = 4; };
for (key in point) println(key + "=" + point[key]);
```

`object.flat()` produces a new user-defined object whose sole prototype is `Object`.
It copies own and inherited properties shallowly, except properties of `Object`
itself. Duplicate keys follow normal prototype lookup precedence, including lexical
prototype contexts. All copied properties are mutable, even when originally constant.
The original object and its prototypes are unchanged. Use `for (key in object.flat())`
to enumerate the materialized properties. For arrays and other builtin types, `flat()`
returns the same receiver. Like `clone` and `instanceOf`, it is an ordinary overridable
method; a detached call (`const flatten = Object.flat; flatten(value)`) accepts the value explicitly.

This describes current behavior. The design for retaining hidden/shadowed cells and
adapting methods during `flat()` is in [appendix I](appendix-i-access-control.md);
the [examples](access-control-examples.md) specify the expected future semantics.
