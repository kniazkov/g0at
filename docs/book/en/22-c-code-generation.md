# 22. C Code Generation

[Contents](index.md) · [Русский](../ru/22-c-code-generation.md) · [Previous chapter](21-ast-transformations.md) · [Next chapter](23-native-abi.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-22-1"></a>

## 22.1. Why C is the intermediate language

After analysis, some functions are known to support execution with fixed numeric types. The next task is to express their computations so that an external compiler can produce machine code. Goat uses C text as its intermediate representation at this stage. The backend (the part that produces target code) has no machine-instruction generator of its own.

The result is a module of specializations, not a standalone C program. It contains no `main`, Goat source reader, or main VM loop. Top-level execution, `println`, and unsupported functions remain in bytecode. Producing a `.c` file therefore does not mean that the entire program has been translated to C.

The inputs are the AST and the proofs from part IV. The outputs are module source, the number of emitted specializations, and failure details. Building a library comes later.

<a id="section-22-2"></a>

## 22.2. Module inventory and dependencies

[c_module.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_module.c) traverses the tree while accounting for preserved original nodes. Candidates are analyzed, pure specializations marked `supported`, without blockers, and with integer or real parameters and results.

Functions receive ordinal identifiers starting at 1; specialization entries start at 0. Signatures are sorted by parameter count and types, so call-observation order does not determine output order. A name such as `goat_f1_i_` denotes the first function's integer specialization; `r_` denotes a real parameter. These are generator names, not source variable names.

Identifiers belong to this module. Adding a function to the source may change the numbering; they are not persistent names across program revisions.

Each static call requires another module entry with the exact signature. If a dependency is missing, the calling specialization is excluded. Checking repeats until the inventory stabilizes. Prototypes for surviving functions precede their definitions, allowing the C compiler to see mutually recursive calls as well.

<a id="section-22-3"></a>

## 22.3. An expression is a value plus preparation

C's argument-evaluation order does not establish the order Goat requires. The generator therefore cannot merely copy an expression into a string. An expression representation contains its type, value text, and a *prelude*: actions to perform before using that value.

[c_arithmetic.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_arithmetic.c) transfers those actions into the surrounding sequence and saves operands in temporaries named `goat_t0`, `goat_t1`, and so on. Binary arithmetic evaluates the left operand before the right. [c_call.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_call.c) handles arguments right to left; the C call itself receives already evaluated temporaries in parameter order. Extra arguments are also evaluated, although the called function does not use them.

Each local has one proven representation: `int64_t`, `double`, or `bool`. Declarations and assignments preserve it. A condition becomes a branch; `return` returns the required numeric type. The generator uses proofs for the particular specialization and accepts an optimized AST replacement only where it agrees with those proofs.

<a id="section-22-4"></a>

## 22.4. Numbers must behave as in the VM

Signed C overflow cannot implement Goat overflow: it is undefined behavior in C. For `+`, `-`, `*`, and unary minus, the generator operates on `uint64_t`, using arithmetic modulo \(2^{64}\), then explicitly reconstructs the signed value. Helpers are included only when needed.

Real computations require binary64 (a 64-bit representation with 53 significant binary digits). Finite constants are emitted as exact hexadecimal C literals, including signed zero. Intermediate values are stored in `volatile double` so that rounding occurs at the intended operation boundaries. Compilation disables fast-math and multiplication/addition contraction; the source also rejects `__FAST_MATH__`.

An integer/real comparison cannot always be reduced to converting the integer to `double`: a large integer may round. [c_control.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_control.c) emits helpers that preserve the distinction and also handle `NaN`, infinities, and range boundaries.

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

[c_generation.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_generation.c) builds a function in a private buffer. If it encounters an unsupported node, lacks a proof, or cannot finish the body with a valid return, partial text never reaches the module. The reason and first failing node are retained.

[c_module_output.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_module_output.c) then also removes dependent functions. Successful independent specializations remain available. Generation is transactional at function level: publication follows successful completion, rather than the first successfully emitted line.

> [!CAUTION]
> The supported numeric subset is the one in [chapter 20](20-native-eligibility.md): literals, locals, `+`, `-`, `*`, numeric comparisons, branches, returns, and proven static calls. General generation of division, strings, objects, exceptions, and closures is absent. A function definition is additionally rejected when its combined count of parameters, locals, and temporaries exceeds 128. This bounds the frame before the native stack check.

The omitted-specialization counter describes candidates rejected while forming the module. It is not a count of every Goat function outside the native subset.

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

Body lowering is implemented in [c_lowering.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/c_lowering.c). Numeric-semantics and module-inventory checks are in [check_c_generation.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_c_generation.sh) and [check_c_module.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_c_module.sh). The next chapter explains the adapter code surrounding the computations themselves.
