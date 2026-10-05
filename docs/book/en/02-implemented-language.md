# 2. The Implemented Language

[Contents](index.md) · [Русский](../ru/02-implemented-language.md) · [Previous chapter](01-project-purpose.md) · [Next chapter](03-program-pipeline.md)

Edition 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-2-1"></a>

## 2.1. Begin with Program Behavior

To understand an interpreter, we first need to establish what it executes. The expression `6 / 4` looks familiar, but its result depends on the language rules: it could be an integer, a real number, or an error. Such rules constitute language semantics (the meaning of its constructs and operations).

Goat uses familiar notation: `var`, `const`, `if`, `func`, `return`, braces, and calls with parentheses. Similar notation does not imply complete agreement with C or JavaScript. This chapter covers the implemented behavior needed for the rest of the book. Full reference tables are planned for the appendices.

The examples are stored in a [shared directory](../examples/). After building the interpreter, run them from the repository root. For example, on Linux:

```sh
./goat --native off docs/book/examples/02-values.goat
```

In Windows PowerShell:

```powershell
.\goat.exe --native off .\docs\book\examples\02-values.goat
```

Native execution is disabled here: we are examining the language rules implemented by the VM (the program that executes bytecode instructions).

<a id="section-2-2"></a>

## 2.2. Values and Names

A value has a type that determines its permitted operations. A variable holds a value, but its declaration does not impose a fixed type. In the example below, `value` first holds an integer and then a real number.

[Values and conditions example](../examples/02-values.goat):

```goat
var value = 6;
value = value / 4;
println(value);
const limit = 10;
if (value < limit && value != 0) {
    println("within limit");
}
println(9223372036854775807 + 1);
println(!!"");
println(!!{});
```

Output:

```text
1.5
within limit
-9223372036854775808
false
false
```

`var` declares a variable that can be assigned a new value. `const` declares a constant: reassignment to that name is prohibited. This restriction applies to the binding between the name and its value; it is not a general promise that all associated state is immutable. For example, a constant may hold a function that changes a captured variable.

The main values encountered in programs are:

| Value | Example | Purpose |
|---|---|---|
| Integer | `42`, `-7` | Signed integers with a 64-bit representation |
| Real number | `1.5`, `1.0` | Numbers represented by C `double`, with finite precision |
| String | `"text"`, `"\n"` | Text; `\n` denotes a newline |
| Boolean | `true`, `false` | The result of a comparison or logical operation |
| Missing value | `null` | Used, in particular, for a missing argument |
| Function | `func(x) { return x; }` | A callable value that can be passed or returned |
| User-defined object | `{ var x = 2; }` | A collection of properties produced by executing a block |

A scope (a region of the program in which a name is available) allows local variables to be declared. An inner declaration can shadow an outer one: identical spelling does not necessarily identify the same variable.

This implementation also permits implicit declarations: when binding an unknown name, the analyzer adds a variable declaration for it. A misspelled name therefore does not necessarily cause a compilation error. The book's examples use explicit declarations to make each variable's origin visible. This mechanism is implemented in [name binding](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/analysis.c).

<a id="section-2-3"></a>

## 2.3. Numbers: Familiar Operations, Specific Rules

Integer addition, subtraction, and multiplication wrap modulo \(2^{64}\) on overflow (the low 64 bits of the result are retained). Adding one to the largest signed integer in the example therefore produces the smallest negative integer. Unary negation and increment or decrement also follow this rule.

Division follows the principle of least astonishment: `6 / 4` naturally suggests `1.5`, without losing the fractional part. This is a guide to choosing language behavior, not a universal expectation shared by every programmer: familiarity with integer division in C may suggest a different answer. The rule is therefore stated explicitly. `6 / 3` produces the integer `2`, whereas `6 / 4` produces the real number `1.5`. If an integer result does not fit in signed 64 bits, as when dividing the smallest integer by `-1`, a real number is returned. Division by zero raises an exception. The `%` operator computes the remainder for integer arguments; a nonzero remainder has the dividend's sign. `**` denotes exponentiation with a real result.

In mixed arithmetic, such as `2 + 0.5`, the integer is converted to `double`. A large integer may be rounded by this conversion: the real representation cannot store every 64-bit integer exactly. Mixed comparisons, however, are implemented separately and do not simply convert both numbers to `double` unconditionally.

There are also bitwise operations (operations on individual bits of an integer): `~`, `&`, `|`, `^`, `<<`, `>>`. These differ from the logical operators `!`, `!!`, `&&`, `||`. Comparisons use `<`, `<=`, `>`, `>=`, `==`, `!=`.

These rules can be checked in the implementations of [integers](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/integer.c), [real numbers](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/real.c), and [shared numeric operations](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/number.c). They are requirements for the C generator we will examine later: translating an operator into a familiar C operator without checking its behavior can change a Goat program's result.

<a id="section-2-4"></a>

## 2.4. Conditions and Evaluation Order

`if` selects a branch using a value's truthiness (its interpretation as true or false). `null`, `false`, numeric zero, an empty string, and a user-defined object without properties are false. Nonzero numbers, nonempty strings, functions, and objects with properties are true. Real numbers are tested for inequality with zero; NaN (the special “not a number” value) is also true.

`!` negates truthiness, while `!!` converts it to `true` or `false`. `&&` and `||` return booleans and use short-circuit evaluation (the right side is not evaluated when the result is already determined). Thus `false && f()` does not call `f`. These rules are checked in the [truthiness test](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/functional/logic_truth/program.goat) and [logical truth tables](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/functional/logic_truth_tables/program.goat).

Precedence determines expression grouping: `2 + 3 * 4` means `2 + (3 * 4)`. Evaluation order answers a different question: which action happens first? Ordinary binary operations evaluate the left operand before the right. Function calls use a different order: arguments are evaluated from right to left, followed by the expression identifying the function to call.

[Argument order example](../examples/02-order.goat):

```goat
var x = 0;
const show = func(a, b) {
    print(a); print("|"); println(b);
};
show(x++, x++);
println(x);
```

It prints:

```text
1|0
2
```

Postfix `x++` returns the old value and increments the variable. The right argument receives `0` first, then the left receives `1`. Parameters are not rearranged: `a` receives the left argument and `b` the right. Prefix `++x` returns the incremented value instead. The same distinction applies to `x--` and `--x`.

Call order is explicit in [call bytecode generation and analysis](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/function_call.c). Optimization must preserve it, or expressions that change variables will begin producing different results.

### Repetition with for

A `for` loop repeats a statement while its condition is true. The order is `initial` once, then `condition`, `statement`, `step`, and another condition check:

```goat
for (initial; condition; step) statement
```

The initializer may be an expression or a `var`/`const` declaration. The condition and step are expressions. Any slot may be empty; an omitted condition means `true`. An empty body is written `;`. Braces group several statements, and a single statement needs no braces. Loops and `if/else` may contain each other; `else` belongs to the nearest unmatched `if`.

[Example](../examples/02-for.goat):

```goat
var sum = 0;
for (var i = 0; i < 5; i++) sum = sum + i;
println(sum);
for (var row = 0; row < 2; row++)
    for (var column = 0; column < 2; column++)
        println(row * 10 + column);
```

It prints `10`, `0`, `1`, `10`, and `11`, on separate lines. A variable declared in the header belongs to the loop and is not available after it. The body has a fresh scope on each iteration, including when braces are omitted. A closure capturing the header variable retains the shared counter; a closure capturing a body-local variable retains that iteration's binding.

`return` leaves the containing function immediately; an exception transfers control to its handler. Neither performs the remaining step of that iteration.

> [!CAUTION]
> `while`, `do/while`, `break`, and `continue` are not implemented. There is no general comma-expression operator: `i++, j++` is not a supported step. Multiple declarations such as `var i = 0, j = 3` are permitted in the initializer.

<a id="section-2-5"></a>

## 2.5. A Function Retains Its Environment

`func` creates a function value. A call binds arguments to parameters; `return` ends the call and passes a result to the caller. If a function finishes without a returned value, its result is `null`.

Missing arguments of user-defined functions are replaced with `null`. Extra arguments are evaluated, including their side effects, but have no corresponding parameters. Built-in functions may impose their own argument-count requirements.

A function can use a variable from an enclosing scope. This combination of a function and its retained environment is called a closure. It allows work with data to continue after the call that created the data has finished.

[Counter example](../examples/02-functions.goat):

```goat
const make_counter = func(start) {
    var value = start;
    return func() {
        value = value + 1;
        return value;
    };
};
const next = make_counter(10);
println(next());
println(next());
const first = func(a, b) { return a; };
println(first());
```

Output:

```text
11
12
null
```

The call to `make_counter(10)` has finished, yet the returned function still accesses `value`. It retains access to the variable, not just the number `10`: the second call observes the first call's change. The constant `next` holds this function and is not reassigned.

Functions can call themselves recursively (enter their own body again with new arguments). The Fibonacci example in Chapter 1 does exactly that. Environment creation, argument passing, and retention of captured data are implemented in the [function object](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/function.c).

<a id="section-2-6"></a>

## 2.6. Blocks as Objects and Exits through Exceptions

Braces in Goat can form an expression. When such a block completes normally, its local data becomes the resulting object. This allows a function to return several named values, for example. A nested block can also access outer variables; explicit `var` helps distinguish creating a local property from changing an outer name.

An exception (a value passed to a handler instead of continuing normal execution) provides another exit path. `throw` can pass any Goat value, and `catch` binds it to the specified name. Operation exceptions use the same mechanism.

[Objects and exceptions example](../examples/02-objects-exceptions.goat):

```goat
const point = {
    var x = 2;
    var y = 3;
};
println(point);
try {
    println(1 / 0);
} catch (error) {
    print("caught: "); println(error);
}
try {
    throw "stop";
} catch (error) {
    println(error);
}
```

Output:

```text
{"x":2,"y":3}
caught: DIVISION_BY_ZERO
stop
```

On division by zero, `println(1 / 0)` does not reach the point of printing a result: control transfers to the nearest applicable handler. Execution continues after the handler finishes. Without a handler, the exception is reported as an uncaught error and the run fails.

Here `DIVISION_BY_ZERO` is a string error value, not an instance of a mandatory exception class. Such standard values are collected in the `Exceptions` object. Block-result construction is described in the [statement list](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/graph/statement_list.c), and handler execution in the [VM](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/vm.c). Checking examples include [returning an object](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/functional/return_object/program.goat) and [an uncaught exception](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/test/functional/throw_uncaught/program.goat).

<a id="section-2-7"></a>

## 2.7. What the Environment Provides

Built-in functions (functions whose implementations are supplied with the interpreter) are available before user code runs. `print` outputs a value without adding a newline, while `println` appends `\n`. Both return `null` and require at least one argument; use `println("")` to print an empty line. Extra arguments are evaluated but are not printed. `input` reads input, and `int` converts to an integer. The numeric set includes, for example, `abs`, `sqrt`, `sin`, `cos`, `min`, `max`, `floor`, `ceil`, and `round`. `pi` and `Exceptions` are also available.

Built-in functions are collected in a [single registry](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/builtins/registry.c). This registry associates a name with its implementation and analysis information. Spelling alone does not guarantee built-in behavior: a local declaration of `print` can shadow the built-in function.

For the following chapters, keep three questions separate. Can a construct be written in source code? Can the VM execute it? Can the analyzer prove enough properties to generate C?

> [!CAUTION]
> For example, a counter with a mutable captured variable works in the VM but falls outside the supported native numeric subset. Objects being available in the language likewise does not imply a native representation for arbitrary objects.

Next, we will follow these boundaries through a single program.