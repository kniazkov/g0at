# 16. Expression and Control-Flow Analysis

[Contents](index.md) · [Русский](../ru/16-expression-and-control-analysis.md) · [Previous chapter](15-abstract-state.md) · [Next chapter](17-call-analysis-and-specializations.md)

Revision 4. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-16-1"></a>

## 16.1. An operation transforms information

A transfer function describes how an operation changes abstract values and state. For addition, it receives operand descriptions and produces a sum description; assignment additionally changes a variable entry. In Goat, AST methods coordinate evaluation, while files in `src/analysis` implement domain operations.

An ordinary binary node evaluates its left operand, then its right. If the first has already ended the normal path, the second must not be analyzed as executed. `BOTTOM` and control mode carry that fact onward. Calls use a different order: arguments right to left, then the callee value, matching the VM.

This is not cosmetic. In `f(x++, x++)`, changing traversal order changes argument values and the later state of `x`. The abstract model must follow language semantics, rather than a convenient child-traversal order.

<a id="section-16-2"></a>

## 16.2. Arithmetic precision

[addition.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/addition.c), [subtraction.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/subtraction.c), and [multiplication.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/multiplication.c) distinguish constants, integer ranges, and broad numeric types. Constants retain concrete results. Interval bounds are computed where possible.

Integer constant operations use the same saturating helpers as the VM. Interval addition and subtraction saturate their endpoint results; multiplication takes the minimum and maximum of four saturated endpoint products. Unary negation reverses and saturates the endpoints. Overflow therefore need not discard all interval information. These rules apply to each operation in evaluation order; saturation does not permit reassociation.

[division.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/division.c) accounts for zero, exact integer quotients, and real promotion. Dividing two arbitrary integers generally produces `numeric`: some arguments yield an integer, others a real. The minimum integer divided by `-1` is handled separately. `%` and `**` have their own rules in [modulo.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/modulo.c) and [power.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/power.c).

An abstract normal result does not always prove freedom from exceptions for every input. If a divisor may be zero or nonzero, describing successful quotients does not describe the error path. An optimizer therefore needs more than a suitable result type.

<a id="section-16-3"></a>

## 16.3. Comparisons, truthiness, and bits

[comparison.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/comparison.c) computes a known boolean when constants or bounds prove it; otherwise, `boolean` remains. Integer endpoints are not unconditionally converted to `double`, which would lose some distinctions between large integers.

`lattice_truth` has four outcomes: `ABSTRACT_TRUE`, `ABSTRACT_FALSE`, `ABSTRACT_EITHER`, and `ABSTRACT_NEVER`. The last means no normal value, rather than another kind of boolean `false`. An interval excluding zero is true; an arbitrary string may be empty and therefore yields `EITHER`.

Bitwise operations in [bitwise.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/bitwise.c) work in the integer domain, computing constants and selected useful constraints. A mask, for example, can bound a result range. An unsupported type or a known invalid shift must not turn into an arbitrary successful result.

Prefix and postfix updates use [update.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/update.c). Both write the new value, but return different descriptions: prefix returns the new value, postfix the old. Reducing both to one pure arithmetic expression would lose the state write.

<a id="section-16-4"></a>

## 16.4. if and short-circuit evaluation

In [if_else.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/graph/if_else.c), a known condition selects one branch; an unknown condition clones state, executes both branches, and joins the results. An absent `else` means a second path without a body. A terminated branch contributes no current values to the continuation.

Logical `&&` and `||` in [logic.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/graph/logic.c) also branch evaluation. If the left operand already determines the result, the right and its effects are skipped. With uncertain truthiness, analysis must account for both evaluating and skipping the right operand.

File [16-control.goat](../examples/16-control.goat):

```goat
var count = 0;
false && ++count;
true || ++count;
var result = 10;
if (input()) { result = 20; } else { result = 30; }
println(count);
println(result);
```

With input line `yes`, the result is:

```text
0
20
```

With an empty line:

```text
0
30
```

Both `++count` expressions are skipped. Analysis must likewise not attribute an executed write to them. The final conditional's branch is unknown in advance, but the possible result values after joining are known.

### A loop needs facts for every iteration

A first visit with `i = 0` cannot justify replacing `i < n` or a branch inside the loop for all later visits. [`for.c`](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/graph/for.c) therefore computes a loop-head invariant (facts covering every arrival at the condition). It evaluates one abstract condition/body/step pass, joins the continuing back edge with the previous head, and repeats. Returns contribute to function-return information but do not flow back to the condition.

[`widen_loop_state`](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/abstract_state.c) broadens changing constants and intervals to type domains. For example, a counter growing from `0` and `1` becomes `integer` instead of producing an endless chain of larger intervals. Unchanged facts remain available. When the current facts stabilize, the condition is evaluated from that invariant to describe exit. A false first condition preserves zero-trip facts; an always-true invariant condition provides no normal exit. This is not a proof of termination.

An invariant describes the allowed states at every entry to the condition, not the final result. For example, in `var total = 0; for (var i = 0; i < n; i++) total = total + i;`, the initial facts are `i = 0` and `total = 0`. After widening, it is sufficient to retain “`i` and `total` are integers”: another abstract pass no longer changes these facts. This stable state is called a fixed point. It permits integer operations in C even when the final sum is unknown. Analysis does not repeat the body `n` times: a billion actual iterations does not mean a billion abstract passes. The analysis budget limits compiler work, not program execution time.

The ordinary reachability pass uses this abstract execution without recording a first iteration as a shared AST constant. Specialization-specific expression proofs intersect observations across passes, so an early constant cannot survive a later incompatible observation.

> [!CAUTION]
> A loop is limited to 16 abstract passes and shares the existing evaluation budget, initially 1024, with calls and nested loops. If stabilization fails, current facts and return information become unknown, and the signature cannot obtain native approval from the unfinished analysis. Exit tests do not narrow counter intervals; a known finite source loop may therefore retain only its variable types.

<a id="section-16-5"></a>

## 16.5. Returns and unreachability

A `return` first analyzes its expression. Its normal result joins the shared return-value accumulator, and control becomes `FLOW_RETURN`. Statements after the return on that path no longer execute. Reaching the end of the function contributes a possible `null`.

With an unknown condition, one path may return while another continues. Thus, “the body contains a return” does not mean “the function always returns here.” Returned values, variable observations, and the possibility of ordinary continuation are combined separately.

A later, separate pass in [reachability.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/analysis/reachability.c) sets `NODE_FLAG_UNREACHABLE` and `immediate_value`. This prepares proofs for changing the shared AST. It is more conservative than ordinary call analysis: a user call forgets current facts, and a function body does not execute merely because its creation expression is encountered. One investigated call's result does not become a global property of the shared body.

<a id="section-16-6"></a>

## 16.6. Exceptions: execution is ahead of analysis

In the VM, `try/catch` handles an exact thrown value and context unwinding. Equally precise abstract analysis would require separate exceptional-exit states and their transfer into the handler.

> [!CAUTION]
> The current implementation has no such model. The `try/catch` node's `execute` method in [try_catch.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/graph/try_catch.c) forgets known values and preserves possible continuation; it does not compute precise body and handler states. Ordinary known-call analysis declines precise traversal of a body containing `try/catch`, while type analysis marks the attempt incomplete.

An explicit `throw` or a definitely failing operation can end the normal path. This helps, but does not replace precise modeling of where the error is caught and which changed variables reach that handler. Executing a construct and analyzing it precisely are different capabilities.

<a id="section-16-7"></a>

## 16.7. Where facts become broader

Precision is lost for several reasons: the domain cannot express the result set, an operand type is unknown, a call may change its environment, or a needed control transfer is not yet modeled. `TOP` in a log therefore need not indicate a defect; it often honestly marks a proof boundary.

> [!CAUTION]
> Condition analysis is currently not a constraint solver, and abstract operator support is broader than the native subset. A precise result for `%`, a shift, or a logical operator does not by itself permit generation of the corresponding C function.

Programs listed in [list.txt](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/test/analysis/list.txt) and reachability tests in [test_reachability.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/test/test_reachability.c) check behavior. They compare not only result types but also writes that should or should not occur under the specified evaluation order.
