# 14. Abstract Values and the Lattice

[Contents](index.md) · [Русский](../ru/14-abstract-values.md) · [Previous chapter](13-builtins.md) · [Next chapter](15-abstract-state.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-14-1"></a>

## 14.1. What can be known without running a program

An interpreter computes a concrete value. A static analyzer tries to establish in advance which values are possible. After input, for example, it does not yet know the string, but knows its type. After choosing between `2` and `5`, it can retain range information. Such descriptions are called abstract values.

Abstract interpretation performs operations on these descriptions. Adding two known integers produces a known integer; adding two arbitrary integers produces an integer description. The program does not run in the VM, input is not read, and output is not printed. The analyzer executes its model of operations.

The goal is information reliable enough for later decisions: remove an unreachable branch, replace an expression with a constant, or select a fixed numeric representation in a native function. Precision is useful only while it avoids false assertions. “Unknown” leaves work to the VM; a falsely proven constant can change the program.

<a id="section-14-2"></a>

## 14.2. From one number to a set of possibilities

The domains in [lattice.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/lattice.h) retain different amounts of information:

| Abstract value | What it describes |
|---|---|
| Integer constant `3` | One integer value |
| Range `[2..5]` | Every integer from 2 through 5 |
| `integer` | Any integer |
| `real` | Any real number |
| `numeric` | An integer or a real |
| `not null` | Any value except `null` |
| `TOP`, printed as `⊤` | Any value |
| `BOTTOM`, printed as `⊥` | No possible value |

Besides numbers, there are string constants and strings in general, boolean constants and the boolean type, functions and known functions, and user-defined objects. `null` is a separate concrete value. It equals neither `TOP` nor `BOTTOM`.

Precision can be understood through set inclusion: `3` is more precise than `[2..5]`, the range is more precise than `integer`, and `integer` is more precise than `numeric`. But integers and strings do not follow each other on one linear scale. The order of the C enumeration does not define precision either.

<a id="section-14-3"></a>

## 14.3. Why a lattice is needed

A lattice is a structure defining a join and a meet for a pair of descriptions. In Goat these operations are `lattice_join` and `lattice_meet`, implemented in [lattice.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/lattice.c).

A join is used when execution could arrive along different paths. It must cover both paths' possibilities. A meet expresses simultaneous constraints. The examples below concern domain operations, rather than special Goat syntax:

| Operation | Result |
|---|---|
| `join(2, 5)` | `[2..5]` |
| `join(integer, real)` | `numeric` |
| `join(integer, string)` | `not null` |
| `join(integer, null)` | `TOP` |
| `meet([2..5], [4..8])` | `[4..5]` |
| `meet(integer, string)` | `BOTTOM` |
| `join(BOTTOM, X)` | `X` |
| `meet(TOP, X)` | `X` |

Joining `2` and `5` already loses information: `3` and `4` were absent from the original alternatives. This is an acceptable overapproximation. The current domain has no separate “exactly 2 or 5” element, so it uses a covering interval.

> [!CAUTION]
> Having `meet` does not mean program conditions automatically refine variables. Current `if` analysis does not add a constraint such as `x < 10` to the true branch, nor retain general relations between variables such as `x == y`. Pass capabilities must be distinguished from operations available in the domain library.

<a id="section-14-4"></a>

## 14.4. BOTTOM is not a language error

`BOTTOM` means no ordinary value. It describes an empty constraint intersection or an operation with no normal result. Dividing a known number by known zero, for example, has no ordinary quotient; VM execution throws an exception.

But `BOTTOM` does not carry the exception value and is not a `DIVISION_BY_ZERO` object. Control state separately records that the ordinary path does not continue. This distinction matters for returns and recursion: no normal result does not prove a particular cause—an exception or nontermination.

The integer-range constructor normalizes bounds. For `min > max` it returns `BOTTOM`; for equal bounds, a constant; and for the full 64-bit integer range, `integer`. One meaning therefore needs no three different storage forms.

<a id="section-14-5"></a>

## 14.5. Floating-point boundaries

A real constant contains a `double`. When comparing descriptions, the lattice treats two `NaN` values as the same abstract constant, although ordinary numeric `NaN == NaN` is false. This compares analyzer information, rather than executing the language's `==` operator.

In contrast, `+0.0` and `-0.0` remain distinct. Their join is `real`, rather than either constant. Numeric comparison considers them equal, but their representation can affect other operations. The analyzer must not silently lose that sign when choosing a constant.

Integer `3` and real `3.0` also differ: their join is `numeric`. Equal numeric magnitudes do not remove the representation distinction that matters to a native interface. [test_lattice.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/test/test_lattice.c) checks these boundaries.

<a id="section-14-6"></a>

## 14.6. Observing a program

File [14-domains.goat](../examples/14-domains.goat):

```goat
var value;
if (input()) { value = 2; } else { value = 5; }
println(value + 1);
```

Analysis of `input()` produces an arbitrary string. It may be empty or nonempty, so both branches are considered. The log contains this line:

```text
#4 14-domains.goat, 2.1: join value = [2..5]
```

From the repository root:

```sh
printf 'yes\n' | ./goat --print-analysis docs/book/examples/14-domains.goat
```

```powershell
'yes' | .\goat.exe --print-analysis .\docs\book\examples\14-domains.goat
```

After the log, the program prints `3`: this run selected the first branch. With an empty line it prints `6`. One execution result does not contradict the analysis range: the range describes both considered paths, while a run follows one.

<a id="section-14-7"></a>

## 14.7. Description storage and model boundaries

Generic elements such as `TOP` and `integer` are singletons; constants, ranges, and known functions are arena-allocated. Descriptions are used as immutable values. States can share them while changing their own references to descriptions. A known user function additionally stores a body node and lexical activation identity; a built-in stores a descriptor.

> [!CAUTION]
> The domain includes `ARRAY` and `TYPED_ARRAY`, but this does not imply arrays exist in this revision's executable language. The domain also lacks real intervals and arbitrary type unions. When it cannot express a precise result, that result expands to a broader element.

An abstract value answers “what is possible.” The next chapter adds “for which declaration, and at which program point, is this known?”
