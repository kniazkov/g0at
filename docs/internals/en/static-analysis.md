# Conditional branches and unreachable code

[Contents](index.md) · [Русский](../ru/static-analysis.md) · [Tests](testing.md)

## The question this pass answers

Before generating bytecode, Goat can prove that some statements cannot be
reached. For example, a false branch of a known true condition cannot execute.
This permits later code generation and simplification to omit work, but only
when the program's observable behavior is preserved.

The analyzer does not run the user's program. It computes **abstract values**:
descriptions such as “exactly 1”, “an integer in this interval” or “unknown”.
An **abstract state** associates declarations with those descriptions and tracks
whether execution can continue normally. An AST is the tree of parsed program
constructs; a node represents an expression, statement or another construct.

## Route through the implementation

| Entry or structure | Responsibility |
| --- | --- |
| analyze() in [analysis.c](https://github.com/kniazkov/g0at/blob/master/src/analysis/analysis.c) | Coordinates binding, interpretation, function analyses and transformations. |
| mark_unreachable_code() in [reachability.c](https://github.com/kniazkov/g0at/blob/master/src/analysis/reachability.c) | Restores replacements, clears stale proofs and starts a fresh immediate-execution traversal. |
| visit_reachable_node() | Dispatches the node's `analyze_reachability` method, or marks a subtree dead when control can no longer reach it. |
| `analyze_reachability` in [if_else.c](https://github.com/kniazkov/g0at/blob/master/src/graph/if_else.c) | Evaluates the condition, chooses or joins paths, and records impossible branches. |
| lattice_truth() in [lattice.c](https://github.com/kniazkov/g0at/blob/master/src/analysis/lattice.c) | Converts an abstract value into information about its possible truth values. |
| join_abstract_states() in [abstract_state.c](https://github.com/kniazkov/g0at/blob/master/src/analysis/abstract_state.c) | Combines paths without losing possibilities that may reach the next statement. |

The normal optimized route is: abstract interpretation and function analyses,
then immediate reachability, property classification, simplification and unused
binding elimination. Optimization disabled with `--optimize none` bypasses these
optimization passes. Programs requiring access-safe or concurrency-safe
optimization take a conservative route that avoids this ordinary pruning pass;
see the guards in analyze().

The `execute` and `analyze_reachability` methods of an if node have different
jobs. The first updates abstract values during interpretation and reports joins.
The second recomputes facts valid **at this execution point** and marks dead
subtrees. An accumulated declaration summary describes values seen across
execution; it is not a substitute for the current state at one condition.

## Reading a condition

lattice_truth() has four results:

| Result | Meaning |
| --- | --- |
| `ABSTRACT_TRUE` | Every represented value is truthy. |
| `ABSTRACT_FALSE` | Every represented value is falsey. |
| `ABSTRACT_EITHER` | The information does not exclude either outcome. |
| `ABSTRACT_NEVER` | There is no normal value; this is not another spelling of false. |

For example, an integer interval wholly above or below zero is truthy. An
interval containing both zero and nonzero values does not determine a branch.
These are properties of the represented values, not guesses about likely input.

The if visitor first visits the condition. This step can change variables or
end normal control flow. If it ends that flow, both branches are marked dead.
Otherwise a proven truth value selects one branch. With an unknown condition,
the visitor clones the post-condition state for both paths; without an else,
the false path simply keeps that state. A join merges the results. Only paths
that continue normally contribute current variable values to the continuation;
returning paths must not constrain later statements. The join creates a new
state and replaces the caller's pointer slot, destroying the old state and clones.

## Examples checked by the analysis suite

Each source below has a hand-written `.expect` companion. Doxygen embeds the
actual test files; GitHub readers can open them via the links. See
[how expectations work](testing.md) for the selector syntax.

### A condition proven true

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_known_true.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_known_true.expect)

<!--! \include{lineno} dead_known_true.goat -->

The literal `true` selects the first branch. The expectations require one
unreachable event at the else subtree on line 3 and none on line 2. They do not
claim that every descendant produces its own event.

<!--! \verbinclude dead_known_true.expect -->

### A condition the pass cannot resolve

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_unknown.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_unknown.expect)

<!--! \include{lineno} dead_unknown.goat -->

This fixture deliberately uses `pi`, whose value is not precise enough for this
pass to choose a branch. Knowing the mathematical constant as a reader does not
supply a missing implementation proof. Both paths remain possible to the
analyzer; `none unreachable 0 0 - -` forbids any unreachable event in this case.
This tests a limit of the current implementation, not a promise of maximal precision.

<!--! \verbinclude dead_unknown.expect -->

### Assignment in a condition

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_assignment_condition.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_assignment_condition.expect)

<!--! \include{lineno} dead_assignment_condition.goat -->

Evaluating the first condition assigns 1 to `x`; the selected body then assigns 2.
The next condition is therefore also truthy. The expectations require the two
else subtrees on lines 3 and 5 to be unreachable. They verify the analyzer's
branch decisions; they are not a bytecode execution test of assignment retention.

<!--! \verbinclude dead_assignment_condition.expect -->

### Both branches return

[Source](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_both_return.goat) · [Expectations](https://github.com/kniazkov/g0at/blob/master/test/data/analysis/dead_both_return.expect)

<!--! \include{lineno} dead_both_return.goat -->

The condition remains unresolved, but either branch returns. The join therefore
has no normal continuation. The following print statement is unreachable;
expectations require that event on line 3 and forbid dead-branch events on lines
1 and 2. A known condition is not necessary to prove the continuation dead.

<!--! \verbinclude dead_both_return.expect -->

## Where the proof goes, and what it does not permit

mark_unreachable_subtree() sets `NODE_FLAG_UNREACHABLE` on all descendants and
clears their cached immediate values. The optional collector records **one event
at the subtree root**. Thus a flag on every node and an event for every node are
different claims. `condition_truth` on the if node guides bytecode generation;
`immediate_value` on an expression is a pointwise fact from this traversal.

A known condition may still have side effects. The if bytecode generator retains
evaluation of a nonliteral condition before emitting the chosen branch.
Replacing the whole if with a branch in `simplify` requires a separate
can_discard_expression() check. Reachability and permission to delete an
expression are distinct proofs.

Creating a function object does not visit its deferred body as though it ran at
its declaration. Function analysis has its own machinery. This pass also does
not refine unknown branch states by assuming the condition true on one side and
false on the other: it clones the state, visits the paths and joins. It is a
conservative traversal, not a general solver for arbitrary program conditions.
