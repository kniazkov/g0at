# Abstract values, state and bounded analysis

[Contents](index.md) · [Русский](../ru/abstract-values.md)

## A set of possibilities, not a sample execution

An abstract value describes possible runtime values. TOP allows any value;
BOTTOM allows none. BOTTOM is not Goat null. An integer constant is more precise
than an interval, which is more precise than INTEGER. Enum numbers do not encode
this ordering. lattice_join() covers alternatives; lattice_meet() intersects
constraints. Joining constants 1 and 3 may include 2: this deliberate loss of
precision is safe because the result still covers both paths.

Arithmetic in `src/analysis` follows the runtime's domains and edge cases. Exact
answers are retained only when justified; otherwise a type or TOP is returned.
Do not implement folding by blindly applying C signed arithmetic: overflow,
division edge cases, NaNs and signed zeros need the existing semantic helpers.

## Current fact versus history

`abstract_state_t` maps declaration identities to current values and accumulated
summaries. A current value answers what is known at this point. A summary includes
values seen earlier and is later flushed into the declaration. It cannot replace
the current value for a branch decision.

clone_abstract_state() isolates mutable maps using copy-on-write. Declarators,
immutable lattice elements, the arena and observation collector remain borrowed.
join_abstract_states() combines summaries from both paths but takes current facts
only from paths with normal continuation. A return branch contributes observations
without contributing a state to the following statement.

## Example: an interval after a join

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/addition_range.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/addition_range.expect)

<!--! \include{lineno} addition_range.goat -->
<!--! \verbinclude addition_range.expect -->

Here `pi` does not provide a precise condition to this analysis. The join covers
`x = 1` and `x = 3`; adding 2 gives the asserted interval `[3, 5]`. Mixing the
interval with a real or a string retains the respective domain, not a fabricated
exact answer. The test observes writes directly; printed output would exercise
only one runtime path.

## Calls, loops and concurrency limit knowledge

Unknown calls invalidate facts that cannot safely survive their effects. Known
calls distinguish the caller's locals from captured state. Recursive calls and
loops cannot simply be unrolled without bound. widen_loop_state() joins a back
edge and promotes changing payloads to type domains until current facts stabilize.
Losing a constant here is intentional, not evidence of a failed execution.

Function return solving has its own bounded fixed point, explained in
[functions](functions.md). Exhaustion leaves an inconclusive result. Access-sensitive
or concurrent programs use the conservative path in analyze(): shared mutable
facts and observable scope identities prevent ordinary pruning; only the restricted
safe expression simplifier runs. Adding a proof must preserve these restrictions.

## Rewrites consume evidence

simplify_graph() runs after reachability and property classification. Replacement
nodes retain originals so restore_graph() can prepare another analysis. A known
result does not by itself authorize deleting the computation: effects, calls and
exceptions matter. can_discard_expression() is stricter than a purity bit.
See [branches](static-analysis.md) for concrete effect-preserving examples.
