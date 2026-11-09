# Conservative analysis of operator calls

[Русский](operator-analysis.ru.md)

Checklist step [#495](https://github.com/kniazkov/g0at/issues/495) prepares the
analyzer for user-defined operator dispatch. It does not connect VM operator
instructions to handlers; those integrations remain separate checklist steps.
The bytecode format and instruction set do not change.

## Built-in proof versus possible dispatch

An unknown value, a non-null value of unknown type, or a user-defined object may
select a user handler. Proven built-in scalars, arrays and function values cannot
replace their operators through this protocol. The decision concerns the left
operand of binary operations and the operand of unary operations; a user object
on the right does not introduce right-hand dispatch.

Possible user dispatch is treated as a call that may mutate state, throw, and
return an arbitrary value. Arithmetic and updates cannot infer their result from
the sign alone. In particular, a user `++` is not modeled as `+ 1`. Comparison
and logical-negation handlers have a boolean normal result, but may still have
effects or fail their runtime result contract. Logical `&&` and `||` handlers
may return arbitrary values.

The current analysis does not specialize an operator from a property value seen
once. Handler properties, aliases and prototype winners may change between
calls; precise dispatch would require a separate stability proof.

## Reachability and simplification

Truth information alone does not prove a built-in short-circuit path. When user
dispatch is possible, analysis retains the logical right-hand side and accounts
for its effects. Implicit equality comparisons in `switch` follow the same effect rules.
A possibly overloaded comparison of a value with itself is not
replaced with a constant. Operator evaluation cannot be discarded merely because
its normal abstract result is constant.

The existing `!!` conversion remains a separate, non-overloadable built-in
operation. It is not two applications of overloaded `!`. Internal dictionary-key
identity and ordering are also unchanged.

A proven invalid built-in operation remains an executable, catchable runtime
exception at the same point. Analysis may record that it has no normal result,
but must preserve earlier effects and the surrounding catch/finally behavior.
An unexecuted branch does not throw merely because analysis inspects it.

## Native specialization and tests

Scalar specializations can keep their existing native fast paths when the
receiver's built-in type is proven for that specialization. Unknown user
operator effects are not evidence of a pure function or a native-safe body.
Receiver proofs are kept per signature. Solved call results and complete
call-target coverage prevent provisional broad discovery profiles from spoiling
a proven scalar specialization. Refinement preserves caller-local facts only
using callee purity established in an earlier completed round; missing coverage
or iteration limits remain conservative. This step does not claim native
user-handler dispatch, which belongs to #503.

Tests combine abstract-result, state-effect, reachability and native-proof
assertions with optimized/unoptimized execution of the currently implemented
runtime. Keeping a future overloaded logical RHS in the graph is distinct from
claiming that the current VM already invokes that handler.
