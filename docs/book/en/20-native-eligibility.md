# 20. Native Compilation Eligibility

[Contents](index.md) · [Русский](../ru/20-native-eligibility.md) · [Previous chapter](19-effects-and-purity.md) · [Next chapter](21-ast-transformations.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-20-1"></a>

## 20.1. What is admitted to compilation

Native code uses representations chosen in advance. It cannot suddenly turn an integer local into a string object in one instruction when the generated function expects `int64_t`. The analyzer therefore checks a particular specialization, rather than merely the presence of arithmetic in the source.

Eligibility combines independent conditions: fixed parameters and result, purity, allowed captures, representable expressions, and a fully supported body. One permitted operation inside an unsupported function does not make the whole function eligible.

These checks precede C generation. `supported` means proven membership in the current subset; it promises no success from the external compiler or loader. Those are later stages in part V.

<a id="section-20-2"></a>

## 20.2. The numeric contract

[c_contract.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/c_contract.c) classifies parameters and results. `integer` maps to `int64`, and `real` to `double`. `numeric` is insufficient: it combines two representations rather than selecting one. `TOP`, `not null`, and `BOTTOM` likewise specify no required concrete interface.

Strings, booleans, and other known nonnumeric types are outside the current external contract. A boolean may nevertheless exist inside a body, for example as a comparison result. Call-boundary restrictions differ from internal-computation restrictions.

Purity is checked, followed by captures. An immutable reference to a statically resolvable user function is allowed: it needs no environment object to be passed. An arbitrary external numeric `const` receives no such exception.

> [!CAUTION]
> The current native interface is limited to integer and real parameters and results. Passing strings, objects, functions, or booleans through it is not implemented. Capturing an external value, even an immutable number, is also unsupported by this contract.

<a id="section-20-3"></a>

## 20.3. Proof at an expression point

[c_expression.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/c_expression.c) records a node's C representation, possible constant, and `discardable` flag—whether its evaluation may be removed. Records belong to a particular signature. A declaration summary such as `[0..20]` cannot replace one.

If one node is visited with incompatible representations, its proof becomes `unknown`. A constant survives only if observations agree. Signed zero matters; `NaN` is not accepted as a stable constant by this equality check. This is a stricter task than comparing lattice descriptions.

A permitted result does not yet permit removing the expression. Assignment and calls can involve important computation despite a known value. Removal uses a limited set of total operations (ones with a normal result over the considered domain) and checks their children.

<a id="section-20-4"></a>

## 20.4. Locals and the complete body

[c_body.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/c_body.c) reanalyzes a specialization and checks body syntax. Each local declaration receives one representation: `int64`, `double`, or `bool`. Every write must agree with it. Parameters already have fixed representations from the signature.

Supported constructs include numeric literals, suitable local-variable reads, unary `+` and `-`, `+`, `-`, `*`, numeric comparisons, local declarations and assignments, numeric `++`/`--`, branches, `for` loops, numeric returns, and proven static calls. Individual node methods also check operands and context; an operator-name list cannot replace these conditions.

Checking covers the body, rather than only expressions successfully visited in one run. `if` has a specific allowance: if its condition is proven constant and discardable in this specialization, checking the chosen branch suffices. An arbitrary unvisited branch cannot simply be ignored.

> [!CAUTION]
> This revision's native subset has no general execution of `/`, `%`, `**`, logical and bitwise operators, `try/catch`, `throw`, closure creation, or arbitrary objects. Eliminating an unreachable branch and folding a constant are separate transformations; they do not constitute general operator support.

A loop additionally needs stable representations through its back edge. All four parts must have body/expression proofs. Prefix and postfix updates are admitted only for mutable numeric parameters or locals, with their distinct old/new results preserved. Changing a local from integer to real, writing captured storage, or exceeding the loop-analysis budget prevents this route. A body that was not visited is not automatically approved.

<a id="section-20-5"></a>

## 20.5. Candidate dependencies

A static call must identify an immutably resolvable user function with a suitable exact signature, proven purity, and a numeric result. Extra arguments remain subject to checks: their expressions still execute. These C-call records are stored separately from the general graph's may-call edges.

First, the pass may refine unknown return types through independently proven pure targets. Then specializations needing only a body proof temporarily become `supported` candidates. This allows mutually recursive functions to be checked without requiring either to be finally approved before the other.

Unsuitable candidates are then removed. If a callee loses eligibility, its caller may also be removed on the next check. This continues until stable. Only then is the `body` blocker cleared for remaining candidates.

> [!CAUTION]
> Return refinement and dependent-body checking are limited to 64 passes. Unstable tentative permissions revert to `unknown`; a truncated graph also prevents final approval. A temporary internal pass status must not be treated as a published proof.

<a id="section-20-6"></a>

## 20.6. Reading status and reasons

| Status | Meaning |
|---|---|
| `unknown` | The required proof has not been obtained |
| `unsupported` | The contract found a definitely unsupported category, such as captured data |
| `supported` | Every required check for the current subset passed |

An unsupported body operator may leave `unknown`, rather than necessarily `unsupported`. These words are therefore not a complete reason classification. `c-blockers` provides further information:

| Blocker | What still prevents approval |
|---|---|
| `analysis` | Required initial information is not established |
| `parameters` | No permitted parameter representation |
| `return` | No permitted result representation |
| `effects` | Purity is not proven |
| `captures` | Disallowed captures exist |
| `body` | Complete-body support is not proven |

One specialization may have several reasons. An individual node's `c-compatible` flag and a whole signature's status are not interchangeable either: a number can be representable in C inside a function that is ineligible overall.

<a id="section-20-7"></a>

## 20.7. Approval and two different failures

File [20-eligibility.goat](../examples/20-eligibility.goat):

```goat
const twice = func(x) { return x * 2; };
const outer = func(x) { return twice(x) + 1; };
const half = func(x) { return x / 2; };
const offset = 2;
const captured = func(x) { return x + offset; };
println(outer(3));
println(half(3));
println(captured(3));
```

VM result:

```text
7
1.5
5
```

With `--print-analysis`, integer signatures produce these outcomes:

| Function | Return type | C status | Blockers |
|---|---|---|---|
| `twice` | `integer` | `supported` | `none` |
| `outer` | `integer` | `supported` | `none` |
| `half` | `numeric` | `unknown` | `return\|body` |
| `captured` | `numeric` | `unsupported` | `return\|captures\|body` |

`outer` demonstrates a proven dependency on `twice`. `half(3)` really returns a real, but `half(4)` would return an integer: the signature has no single fixed result representation, and division itself is outside the subset. `captured` is pure by effects, but its capture is unsupported. All four functions nevertheless execute correctly in the VM.

<a id="section-20-8"></a>

## 20.8. From eligibility to transformation

At the pass's end, there is more than a general AST color: signatures, expressions, local representations, and call targets have separate facts. The generator must use the appropriate proof rather than replace it with information from one concrete run.

This part's final chapter examines analysis's second application: changing the AST itself. Requirements are equally strict because the changed tree serves ordinary bytecode generation as well as the native backend.
