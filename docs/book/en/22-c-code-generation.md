# 22. C Code Generation

[Contents](index.md) · [Русский](../ru/22-c-code-generation.md) · [Previous chapter](21-ast-transformations.md) · [Next chapter](23-native-abi.md)

Revision 7. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-22-1"></a>

## 22.1. Why C is the intermediate language

After analysis, some functions are known to support execution with fixed numeric types. The next task is to express their computations so that an external compiler can produce machine code. Goat uses C text as its intermediate representation at this stage. The backend (the part that produces target code) has no machine-instruction generator of its own.

The result is a module of specializations, not a standalone C program. It contains no `main`, Goat source reader, or main VM loop. Top-level execution, `println`, and unsupported functions remain in bytecode. Producing a `.c` file therefore does not mean that the entire program has been translated to C.

The inputs are the AST and the proofs from part IV. The outputs are module source, the number of emitted specializations, and failure details. Building a library comes later.

<a id="section-22-2"></a>

## 22.2. Module inventory and dependencies

[c_module.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_module.c) traverses the tree while accounting for preserved original nodes. Candidates are analyzed, pure specializations marked `supported`, without blockers, and with integer or real parameters and results.

Functions receive ordinal identifiers starting at 1; specialization entries start at 0. Signatures are sorted by parameter count and types, so call-observation order does not determine output order. A name such as `g_f1_i_` denotes the first function's integer specialization; `r_` denotes a real parameter. These are generator names, not source variable names.

Internal names use the short `g_` prefix: parameters `g_p0`, locals `g_l0`, and temporaries `g_t0`. Each function definition is preceded by a comment with its source name, parameters, and source position; unnamed functions use `<anonymous>`. Definitions and support entities are separated by blank lines. Public ABI types and the exported `goat_native_query_v1` name retain their spelling for compatibility.

Identifiers belong to this module. Adding a function to the source may change the numbering; they are not persistent names across program revisions.

Each static call requires another module entry with the exact signature. If a dependency is missing, the calling specialization is excluded. Checking repeats until the inventory stabilizes. Prototypes for surviving functions precede their definitions, allowing the C compiler to see mutually recursive calls as well.

<a id="section-22-3"></a>

## 22.3. An expression is a value plus preparation

C's argument-evaluation order does not establish the order Goat requires. The generator therefore cannot merely copy an expression into a string. An expression representation contains its type, value text, and a *prelude*: actions to perform before using that value.

[c_arithmetic.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_arithmetic.c) transfers those actions into the surrounding sequence and saves operands in temporaries named `g_t0`, `g_t1`, and so on. Binary arithmetic evaluates the left operand before the right. [c_call.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_call.c) handles arguments right to left; the C call itself receives already evaluated temporaries in parameter order. Extra arguments are also evaluated, although the called function does not use them.

Each local has one proven representation: `int64_t`, `double`, or `bool`. Declarations and assignments preserve it. A condition becomes a branch; `return` returns the required numeric type. The generator uses proofs for the particular specialization and accepts an optimized AST replacement only where it agrees with those proofs.

<a id="section-22-4"></a>

## 22.4. Numbers must behave as in the VM

Signed C overflow is undefined behavior. Generated `+`, `-` and `*` helpers use compiler overflow intrinsics when available and otherwise use portable checks before signed arithmetic. Negation checks `INT64_MIN` explicitly. Both paths saturate to the full `int64_t` range and agree with the VM. Defining `GOAT_FORCE_PORTABLE_INTEGER_MATH` exercises the portable path. Helpers are included only when needed.

Real computations require binary64 (a 64-bit representation with 53 significant binary digits). Finite constants are emitted as exact hexadecimal C literals, including signed zero. Intermediate values are stored in `volatile double` so that rounding occurs at the intended operation boundaries. Compilation disables fast-math and multiplication/addition contraction; the source also rejects `__FAST_MATH__`.

An integer/real comparison cannot always be reduced to converting the integer to `double`: a large integer may round. [c_control.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_control.c) emits helpers that preserve the distinction and also handle `NaN`, infinities, and range boundaries.

File [22-numeric-semantics.goat](../examples/22-numeric-semantics.goat):

```goat
const increment = func(x) { return x + 1; };
const equal = func(x, y) {
    if (x == y) { return 1; }
    return 0;
};
const pair = func(a, b) { return a * 10 + b; };
const order = func(x) { return pair(x = x + 1, x = x + 1); };
println(increment(9223372036854775807));
println(equal(9007199254740993, 9007199254740992.0));
println(order(0));
```

Result:

```text
-9223372036854775808
0
21
```

The last line follows from argument order: the right assignment produces `1`, the left produces `2`, and the call is then `pair(2, 1)`. Native execution needs these checks too: formulas that look alike need not behave alike.

<a id="section-22-5"></a>

## 22.5. A complete function or a failure

[c_generation.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_generation.c) builds a function in a private buffer. If it encounters an unsupported node, lacks a proof, or cannot finish the body with a valid return, partial text never reaches the module. The reason and first failing node are retained.

[c_module_output.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_module_output.c) then also removes dependent functions. Successful independent specializations remain available. Generation is transactional at function level: publication follows successful completion, rather than the first successfully emitted line.

> [!CAUTION]
> The supported numeric subset is the one in [chapter 20](20-native-eligibility.md): literals, locals, `+`, `-`, `*`, numeric comparisons, `++`/`--`, branches, `for` loops, returns, and proven static calls. General generation of division, strings, objects, exceptions, and closures is absent. A function definition is additionally rejected when its combined count of parameters, locals, and temporaries exceeds 128. This bounds the frame before the native stack check.

The omitted-specialization counter describes candidates rejected while forming the module. It is not a count of every Goat function outside the native subset.

### Lowering a loop

[`c_emit_for`](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_control.c) emits a C scope, local storage, the initializer, and a repeated block. Condition setup stays inside that block, followed by a false-condition exit, the body, and step setup. It is not moved before the loop: a call or assignment in the condition must execute on each check. Nested body scopes keep their own storage. Integer updates use the same saturating helpers as addition and subtraction; real updates round through a `volatile double` temporary. Postfix update keeps a separate copy of the old value.

Loop iterations and sequential function calls have no cumulative execution budget. A long loop remains native regardless of its iteration count. Calls inside the loop still check simultaneous call depth and stack distance under the policy of chapter 25.

[Example](../examples/22-native-for.goat):

```goat
const sum = func(n) {
    var result = 0;
    for (var i = 0; i < n; i++) result = result + i;
    return result;
};
println(sum(10));
println(sum(5000));
```

`./goat --native required --print-native docs/book/examples/22-native-for.goat` prints `45` and `12497500`. With sufficient stack headroom, the report has two successful native entries and no retries. Neither loop is limited by its iteration count. In PowerShell, use `.\goat.exe` in place of `./goat`.

### The built-in absolute value

[Example](../examples/22-native-abs.goat):

```goat
const sum = func(n) {
    var result = 0;
    for (var i = -n + 1; i < n; i++)
        result = result + abs(i);
    return result;
};
println(sum(100));
```

With `--native required`, the example prints `9900`; `sum` executes natively. The integer operation selects the argument or its saturating negation. Consequently, `abs(INT64_MIN)` equals `INT64_MAX`. The real operation uses C `fabs`: negative zero becomes positive zero, infinity becomes positive infinity, and NaN remains NaN. All arguments are evaluated right to left, including unused extra arguments. No separate native adapter is generated for the built-in.

Native calls also support `acos`, `asin`, `atan`, `cbrt`, `ceil`, `cos`, `cosh`, `exp`, `exp2`, `expm1`, `floor`, `fmod`, `hypot`, `int`, `log`, `log10`, `log1p`, `log2`, `min`, `max`, `pow`, `round`, `sign`, `sin`, `sinh`, `sqrt`, `tan`, `tanh`, and `trunc`. The original built-in binding must be proven stable; immutable aliases are allowed. Arguments are evaluated right to left. Extra arguments are evaluated for their effects and discarded; they need a supported C representation, but need not be numeric.

`atan(y)` maps to C `atan`, while `atan(y, x)` maps to `atan2`. Math functions use binary64 arguments and results. `sign` returns an integer; NaN and both zeros produce zero. For zero ties, `min` returns negative zero if either operand is negative zero, and `max` returns negative zero only if both operands are negative zero. This rule applies equally to the VM, constant folding, and native execution. Other values use C `fmin`/`fmax`, including their NaN handling.

`int` preserves an integer argument and truncates a finite real argument toward zero when it lies in `[-2^63, 2^63)`. Otherwise it returns the supplied fallback or integer zero. The fallback is evaluated even when conversion succeeds.

> [!CAUTION]
> Native `int` currently accepts only integer and real source operands. For a real source operand, an explicit fallback must have integer representation. Boolean and string conversions remain available in the VM. Mutable aliases, unresolved targets, and expressions without a C representation prevent native lowering.

<a id="section-22-6"></a>

## 22.6. Inspecting the result

File [22-native-module.goat](../examples/22-native-module.goat) provides a shared example for the following chapters:

```goat
const twice = func(x) { return x * 2; };
const calculate = func(x) {
    var result = twice(x);
    if (result < 0) { return -result; }
    return result + 1;
};
println(calculate(3));
println(calculate(-3));
println(calculate(1.5));
```

Execution result:

```text
7
6
4.0
```

From the repository root, with `./goat` available, prepare a working copy:

```sh
mkdir -p build/book-native
cp docs/book/examples/22-native-module.goat build/book-native/module.goat
./goat --save-c build/book-native/module.goat
```

This creates `module.c` beside the source. Look for integer and real specializations of both functions, temporaries, forward declarations, and adapters. `calculate` calls the typed C function for `twice` directly; no ABI packing is needed between these two functions.

Body lowering is implemented in [c_lowering.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/codegen/c_lowering.c). Numeric-semantics and module-inventory checks are in [check_c_generation.sh](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/scripts/check_c_generation.sh) and [check_c_module.sh](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/scripts/check_c_module.sh). The next chapter explains the adapter code surrounding the computations themselves.
