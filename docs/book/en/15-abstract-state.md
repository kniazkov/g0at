# 15. Abstract State

[Contents](index.md) · [Русский](../ru/15-abstract-state.md) · [Previous chapter](14-abstract-values.md) · [Next chapter](16-expression-and-control-analysis.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-15-1"></a>

## 15.1. A value belongs to a declaration

A program may have several variables named `x` in different scopes. A “name → value” table would confuse them. Abstract state therefore uses declaration identity: its key is a pointer to the AST declarator bound to the variable. Chapter 7's name binding has already determined which declaration is meant.

`abstract_state_t` in [abstract_state.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/abstract_state.h) contains an AVL tree of these entries, control mode, references to an arena and event collector, and call-analysis data. This is analyzer state. It is not a VM `context_t` and holds no runtime number or string objects.

State changes as the program is traversed. An entry describes what is known at the current point, rather than what a variable will hold in every future run.

<a id="section-15-2"></a>

## 15.2. Current value and summary

Each entry in [abstract_state.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/abstract_state.c) contains `current` and `summary`. Writing a new value replaces `current` and joins `summary` with the new value.

| Step | `current` for `x` | `summary` for `x` |
|---|---|---|
| Declaration `x = 0` | `0` | `0` |
| Assignment `x = 10` | `10` | `[0..10]` |
| Assignment `x = 20` | `20` | `[0..20]` |

The next expression needs the current value. The summary retains observations about the declaration, for example to display analysis results. Substituting the summary for the current value would needlessly lose precision: immediately after `x = 20`, exactly `20` is known.

File [15-state.goat](../examples/15-state.goat):

```goat
var x = 0;
x = 10;
println(x);
x = 20;
println(x);
```

It prints:

```text
10
20
```

With `--print-analysis`, the summary is:

```text
#4 15-state.goat, 1.1: summary x = [0..20]
```

The interval includes values the program never actually assigned. This follows from the domain's form, rather than from extra hidden assignments.

<a id="section-15-3"></a>

## 15.3. Splitting at a branch

When a condition is unknown, the analyzer must consider two versions of state. `clone_abstract_state` creates a separate AVL tree, but initially shares value-pair records between copies. Each pair has a reference count.

Before a shared pair changes, a private copy is created. This is *copy-on-write*. A change in one branch therefore leaves the other unchanged. References to immutable abstract values are copied, rather than all ranges and strings.

States own their trees and pairs. Declarators, lattice elements, and the arena are borrowed: destroying a state does not free them. Either copy can be destroyed first, but the shared arena must outlive both. The collector and some accumulators are deliberately shared: branches contribute events to a common log and results to a common return summary.

<a id="section-15-4"></a>

## 15.4. Joining accounts for paths that exit

`join_abstract_states` combines entries from both inputs. Only `FLOW_NORMAL` paths, those continuing ordinary execution, contribute current values. A path ending in `return` must not affect a variable's value after the conditional: it never reached that point.

Summaries, in contrast, combine both paths. An assignment before a `return` was genuinely considered and belongs to the declaration's history. Ending a path therefore does not erase every observation about it.

If an entry is absent on a continuing path, joining uses abstract `null`. If the path does not continue, its contribution to the current value is `BOTTOM`. These differ: a missing entry does not mean unreachability.

Control modes are joined too. If at least one path continues, the result is `FLOW_NORMAL`. With no normal path, a return is retained if either input has one; otherwise the result is unreachable. This is this pass's contract, rather than a universal model of every possible language control transfer.

<a id="section-15-5"></a>

## 15.5. When facts must be forgotten

An unknown function may modify a captured variable. Keeping an old constant after such a call would be unsafe. `forget_abstract_values` replaces current values with `TOP` and marks root bindings unknown. This loss of precision is deliberate.

Built-in names share a synthetic declarator. Their lookup additionally needs the name text, and `builtin_bindings_unknown` prevents relying on the original binding after a possible change. Otherwise, a user's replacement of `print` could be analyzed as standard output.

Known calls use more precise state transfer. Changes to external variables return to the caller; callee locals do not become the caller's current variables. Their observations may enter summaries. Before analyzing another call, current local values are reset so it does not continue the previous activation.

> [!CAUTION]
> Forgetting after an unknown call is coarse: it affects all tracked entries, rather than only variables proven accessible to that call. Current state has no precise model of all objects and their relationships that would always narrow the possible write set.

<a id="section-15-6"></a>

## 15.6. What remains in the AST

`flush_abstract_state` writes accumulated summaries into `declarator->abstract_value` and creates `summary` events. The temporary state can then be destroyed. Arena values remain available while the graph lives.

This field must not be confused with `expression->immediate_value`, which concerns one point of immediate execution, or with a function summary for a parameter-type tuple. An expression, declaration, and function ask different questions: a result here, values over a declaration's history, and behavior of an entire specialization.

The pass order in [analysis.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/analysis.c) preserves this distinction. Ordinary abstract interpretation and declaration summaries come first, followed by function analysis; later, a separate reachability pass establishes facts for safely changing the shared AST.

<a id="section-15-7"></a>

## 15.7. Contracts that can be tested

[test_abstract_state.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_abstract_state.c) checks clone independence, joining current values and summaries, and releasing shared records. These checks matter even with correct `join` formulas: an ownership error can silently change a neighboring branch.

The analysis log is a sequence of observations from several stages. An early `write x = 10` does not promise that every later read equals `10`. Nor does the final summary replace pointwise knowledge. Entries must be read with their kind and coordinates, rather than as one table of final values.
