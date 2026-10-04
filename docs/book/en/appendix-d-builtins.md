# Appendix D. Built-in Functions

[Contents](index.md) · [Русский](../ru/appendix-d-builtins.md) · [Previous](appendix-c-vm-instructions.md) · [Next](appendix-e-launch-interfaces.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-d-1"></a>

## D.1. Common call rules

[registry.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/registry.c) registers 33 functions. The tables list all of them. `pi` and `Exceptions` are root-environment values, not functions. A name may be shadowed by a user declaration; handling follows the built-in function object, not its spelling alone.

The shared wrapper checks minimum arity: too few arguments produce the string exception `INVALID_ARGUMENT`. Extra arguments are evaluated first and then ignored, except for `int`'s second argument, which supplies a fallback value. Even `println()` without an argument fails; print an empty line with `println("")`.

<a id="section-d-2"></a>

## D.2. Output, input, and special numeric functions

| Function | Minimum | Behavior |
| --- | --- | --- |
| `print(x)` | 1 | Prints only x, without a separator or newline; returns null. |
| `println(x)` | 1 | The same, followed by one newline; returns null. |
| `input()` | 0 | Reads a line without its ending newline; EOF before data gives an empty string. Read failure is INVALID_OPERATION. |
| `int(x, fallback)` | 1 | Converts to integer; on failure returns the second argument unchanged, or 0. |
| `abs(x)` | 1 | Absolute value; preserves integer or real type. |
| `sign(x)` | 1 | Integer −1, 0, or 1; NaN gives 0. |

`int` leaves an integer unchanged and converts `false`/`true` to 0/1. A finite real must lie in [-2^63, 2^63); its fractional part is truncated toward zero. A string must contain an entire signed decimal integer with permitted surrounding ASCII whitespace and no overflow. `NaN`, infinities, unsuitable strings, and other objects produce the fallback, not a conversion exception.

`abs(INT64_MIN)` remains `INT64_MIN` under integer-overflow rules. Nonnumeric arguments to `abs` and `sign` produce `INVALID_ARGUMENT`. Output uses an object's text representation; truthiness and numeric conversion are different operations.

<a id="section-d-3"></a>

## D.3. Math functions

All following functions accept integers and reals, convert operands to `double`, and return a real, including `floor`, `round`, `min`, and `max`. Trigonometric arguments and inverse-function results use radians.

| Function | Minimum | Purpose |
| --- | --- | --- |
| `acos(x)` | 1 | Arc cosine |
| `asin(x)` | 1 | Arc sine |
| `cbrt(x)` | 1 | Cube root |
| `ceil(x)` | 1 | Round upward |
| `cos(x)` | 1 | Cosine |
| `cosh(x)` | 1 | Hyperbolic cosine |
| `exp(x)` | 1 | Exponential e^x |
| `exp2(x)` | 1 | Power 2^x |
| `expm1(x)` | 1 | e^x − 1 |
| `floor(x)` | 1 | Round downward |
| `log(x)` | 1 | Natural logarithm |
| `log10(x)` | 1 | Base-10 logarithm |
| `log1p(x)` | 1 | Natural logarithm of 1 + x |
| `log2(x)` | 1 | Base-2 logarithm |
| `round(x)` | 1 | Round to nearest, halfway away from zero |
| `sin(x)` | 1 | Sine |
| `sinh(x)` | 1 | Hyperbolic sine |
| `sqrt(x)` | 1 | Square root |
| `tan(x)` | 1 | Tangent |
| `tanh(x)` | 1 | Hyperbolic tangent |
| `trunc(x)` | 1 | Truncate toward zero |
| `atan(y, x)` | 2 | Angle from y and x, C atan2(y, x) |
| `fmod(x, y)` | 2 | Floating remainder of x/y |
| `hypot(x, y)` | 2 | Length from two components |
| `max(x, y)` | 2 | Maximum of a pair, C fmax |
| `min(x, y)` | 2 | Minimum of a pair, C fmin |
| `pow(x, y)` | 2 | Power x^y |

A nonnumeric operand produces `INVALID_ARGUMENT`. Domain errors and overflow inside C math are not automatically converted into Goat exceptions: `NaN` and infinities can result. `min`/`max` use `fmin`/`fmax`, including NaN handling; they do not traverse an arbitrary argument list. `fmod` and integer `%` are different operations.

<a id="section-d-4"></a>

## D.4. Abstract results and effects

Analysis handlers perform no input or output. `print`/`println` produce the `null` domain and `output` effect; `input` produces `string` and `input`. All others have `effects=none`, which does not rule out argument errors.

Common math handlers compute a constant for known numbers; otherwise they produce `real` for numeric or still-unknown types. A definitely unsuitable type produces `BOTTOM`. `abs` preserves a known numeric category but does not calculate the exact image of an arbitrary integer range. `sign` can compute a constant or a range within [-1..1]. `int` preserves integer information, gives [0..1] for a general boolean, and joins `integer` with the fallback when conversion success is unknown.

These facts describe normal results, not a complete exception graph. A built-in C implementation also does not mean its call is permitted inside a generated C specialization: that is chapter 20's separate contract.

Sources: [math_function.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/math_function.c), [atan.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/atan.c), [int.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/int.c), [abs.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/abs.c), [sign.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/sign.c), [input.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/input.c), [println.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/builtins/println.c). Checks: [test_builtin_functions.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_builtin_functions.c).
