# 13. Built-in Functions

[Contents](index.md) · [Русский](../ru/13-builtins.md) · [Previous chapter](12-exceptions.md) · [Next chapter](14-abstract-values.md)

Revision 4. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-13-1"></a>

## 13.1. The boundary between language and implementation

A built-in looks like an ordinary call in source, but its executor is already written in C and included in the interpreter. It needs no bytecode body. This differs from a native specialization of a user function, which the system obtains by generating and compiling C code.

Built-ins are collected in [registry.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/registry.c). The described revision has 33. The root environment also provides `pi` and `Exceptions`, but these are not registry functions. Looking up a name in the root object finds a descriptor and obtains its associated function object.

The central registry lets the runtime and analyzer use one set of definitions. Adding a new name still requires a consistent implementation of its behavior, rather than just an entry in the list.

<a id="section-13-2"></a>

## 13.2. One descriptor, two ways to operate

The `builtin_function_t` structure in [builtin_function.h](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/builtin_function.h) defines the common contract:

| Field | Meaning |
|---|---|
| `name` | Name in the root environment |
| `min_args` | Minimum argument count |
| `effects` | Observable side effects |
| `execute` | Execution on concrete objects |
| `interpret` | Computing result information during analysis |
| `get_object` | Obtaining a stable function object |

`execute` receives objects and a thread, returning a normal value or an exception. `interpret` receives abstract values (information about possible values, rather than necessarily the values themselves) and analysis state. It may return, for example, a known number, an arbitrary string, or the absence of a normal result.

Analyzing `input()` does not read the keyboard, and analyzing `println(...)` does not print text. Otherwise, compilation itself would change the program's interaction with the user. For a numeric function with known arguments, the abstract implementation may compute a constant. Abstract values are the subject of the next part.

<a id="section-13-3"></a>

## 13.3. The common call path and effects

The wrapper in [function.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/model/function.c) pops all actual arguments, checks `min_args`, calls `execute`, releases arguments, and passes the result to the VM. Too few arguments produce `INVALID_ARGUMENT`. Extra arguments have already been evaluated; a concrete executor generally uses only the required positions.

This is particularly visible with `print` and `println`: they print the first argument, rather than joining an arbitrary list. An extra expression can still change state or throw an exception before the call. `min_args` specifies a minimum, rather than an exact signature.

Effects identify observable actions. `print` and `println` have `BUILTIN_EFFECT_OUTPUT`; `input` has `BUILTIN_EFFECT_INPUT`; numeric functions and `int` have `BUILTIN_EFFECT_NONE`. The common enumeration also includes `BUILTIN_EFFECT_BINDINGS` for possible changes to variable bindings.

Having no effect does not mean having no error: a numeric function with an unsuitable argument can throw. Nor does `println`'s known `null` result permit simply removing the call: printing remains part of program behavior.

<a id="section-13-4"></a>

## 13.4. The numeric library

The table lists every numeric function in the current registry. These are purpose-based groups, rather than separate namespaces.

| Group | Functions | Minimum arguments |
|---|---|---:|
| Absolute value and sign | `abs`, `sign` | 1 |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos` | 1 |
| Angle from two components | `atan(y, x)` (C `atan2`) | 2 |
| Hyperbolic functions | `sinh`, `cosh`, `tanh` | 1 |
| Exponentials and logarithms | `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p` | 1 |
| Roots | `sqrt`, `cbrt` | 1 |
| Rounding | `ceil`, `floor`, `round`, `trunc` | 1 |
| Pair operations | `pow`, `hypot`, `fmod`, `min`, `max` | 2 |

Common executors in [math_function.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/math_function.c) obtain numeric values, convert them to `double`, and call the corresponding C math function. The result is a real object, so `sqrt(9)` prints `3.0` and `min(4, 2)` prints `2.0`. `min` and `max` operate on a pair, rather than on all supplied arguments.

`abs` separately preserves integer representation for an integer argument. The minimum 64-bit integer saturates to `INT64_MAX`, as with integer negation. `sign` returns integer `-1`, `0`, or `1`; for `NaN`, comparisons with zero are false and the result is `0`.

A nonnumeric argument to a common math function produces `INVALID_ARGUMENT`. A numeric argument outside a function's ordinary domain can, however, produce `NaN` or infinity under the called C function's rules: the implementation does not turn every such result into a Goat exception. An invalid type and a special numeric result must therefore be distinguished.

<a id="section-13-5"></a>

## 13.5. int conversion and a fallback value

[int.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/int.c) implements `int(value, fallback)`, with an optional second argument. An integer is preserved, a boolean becomes `0` or `1`, and a finite real in the permitted range is truncated toward zero. Before casting, the code checks the range from inclusive `-2^63` to exclusive `2^63`; `NaN` and infinities are not converted.

A string is parsed as a decimal integer with an optional sign. Surrounding ASCII spaces and whitespace control characters are allowed. The entire remaining string must be parsed; overflow is checked before adding each digit. Strings with a fractional part or arbitrary suffix do not count as successful conversions.

On failure, the second argument is returned without conversion, or integer `0` if it is absent. The fallback may therefore be a string or another object. This is an intended result, rather than an exception. However, `int()` omits the required first argument and receives `INVALID_ARGUMENT` from the common wrapper.

<a id="section-13-6"></a>

## 13.6. Input and output

[print.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/print.c) converts its first argument to a string and prints it. [println.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/println.c) invokes that same executor and appends `\n` after a successful result. Both return `null`; use `println("")` for an empty line. `println()` lacks an argument.

[input.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/builtins/input.c) flushes `stdout` before reading a line from `stdin`. The line ending is excluded. An empty string is also returned at end-of-input when no characters were read. A read or decoding error produces `INVALID_OPERATION`.

[io.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/lib/io.c) uses `ReadConsoleW` for a Windows console and UTF-8 for ordinary byte streams, including redirected input. How Cyrillic appears in a terminal and the encoding of bytes sent to it are separate questions; inside the program, the result is a string object.

File [13-input.goat](../examples/13-input.goat):

```goat
const text = input();
println(int(text, -1) + 1);
```

Entering `41` and pressing Enter makes the program print `42`. From the repository root, the same example can run with redirected input:

```sh
printf '41\n' | ./goat docs/book/examples/13-input.goat
```

```powershell
'41' | .\goat.exe .\docs\book\examples\13-input.goat
```

> [!CAUTION]
> `input` blocks execution until a line arrives or reading ends. Its return value has no separate EOF indicator: an empty input line and end-of-input without characters both produce the same empty string.

<a id="section-13-7"></a>

## 13.7. A name can be shadowed; the object stays the same

A built-in is identified by its object and descriptor. The spelling `println` alone does not guarantee standard output: a local variable with that name can hold a user function. Conversely, another variable can retain the original `println` object.

The analyzer in [function_call.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/function_call.c) selects an abstract implementation using the descriptor of the known callee value. If the binding is unknown or changed, it cannot apply built-in properties merely from the name's text.

[13-builtins.goat](../examples/13-builtins.goat) combines conversions, an alias, and shadowing:

```goat
println(abs(-3));
println(sqrt(9));
println(min(4, 2));
println(int("  -12  "));
println(int(3.9));
println(int("bad", "fallback"));
println(int("bad"));
const output = println;
{
    const println = func(x) { return x + 1; };
    output(println(4));
}
println("");
try { println(); } catch (error) { output(error); }
```

The output includes one empty line before the error:

```text
3
3.0
2.0
-12
3
fallback
0
5

INVALID_ARGUMENT
```

`output` retains the original built-in object. Inside the block, the local `println` only adds one; `output` prints the result. After the block, the outer `println` name is available again. The final call demonstrates the shared minimum-argument check.

<a id="section-13-8"></a>

## 13.8. Moving on to static analysis

The execution model now establishes concrete requirements for the analyzer. It must account for evaluation order, changes to captured variables, exceptions, and call effects. Knowing the mathematical result is insufficient if computing it also reads input or prints a line.

The built-in's dual interface makes this boundary explicit: `execute` handles the values of one run, while `interpret` handles what can be asserted about possible runs. Part IV starts with representing those assertions and gradually develops them into the proofs required for native generation.
