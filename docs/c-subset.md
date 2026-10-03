# Initial C subset contract

This is the contract for a future backend, not an implemented backend or a public
native-library ABI. The analyzer checks interface preconditions and pointwise numeric expressions.
Control-flow and call proofs must establish complete bodies in later steps.

## Interface and environment

| Goat domain | Initial C representation | Interface decision |
| --- | --- | --- |
| Integer, integer constant or range | `int64_t` | Representable |
| Real or real constant | `double` | Representable |
| `TOP`, `NOT_NULL`, `NUMERIC` | Undetermined | Needs refinement |
| `BOTTOM` | No normal value | Needs a return contract |
| Boolean, null, string, array, function, object | None | Outside this subset |

An integer-valued real remains `double`. A `NUMERIC` union is not silently widened
to `double`: doing so would lose Goat's runtime type and possibly integer precision.
Each formal parameter and the normal return need one fixed representation. Zero
parameters are allowed. Missing arguments are still `null`; extra arguments must
still be evaluated by the caller, even when omitted from the specialization key.

Boolean literals and comparison results are allowed as expression temporaries. This does not admit boolean parameters or returns into this first
numeric interface. There is no boxed-value, string, object or closure ABI yet.

Purity must be proven by the effect pass. Direct effects alone, an analyzed return
type, or a green AST node are not sufficient. A callee's purity does not establish
its C support; every call target also needs a compatible body and signature proof.

There are no captured data slots. Even a pure read of a captured numeric constant
is outside this first subset. Read-only references to `const` functions, including
immutable aliases, are allowed as static call identities. They do not automatically
prove the callee's body or eliminate data captures inside that callee. Mutable and
unresolved function bindings are not static identities. Calls into arbitrary VM or
native functions require a future explicit bridge; names alone are insufficient.

## Required numerical behavior

These are requirements for later lowering; the expression checker below proves
eligibility under this contract, not the existence of an emitter.

- Integers retain the full signed 64-bit range. Addition, subtraction,
  multiplication, negation and updates wrap modulo 2^64. Ordinary overflowing signed
  C arithmetic is forbidden; use the rules in `src/lib/integer_math.h` or equivalent
  defined operations. In particular, negating `INT64_MIN` returns `INT64_MIN`.
- Reals retain Goat's `double` values, including NaN, infinities and signed zero.
  A real-typed parameter does not prove finiteness. A backend must preserve VM
  rounding boundaries and may not enable fast-math, reassociation or contraction
  that changes results. Integer-to-double conversions follow `integer_to_double`,
  including forced rounding on x87 targets.
- Mixed numeric comparisons follow `src/lib/comparison.h`; casting both operands
  to `double` is not sufficient for large integers. Numeric truthiness must match
  the model, including NaN and signed zero.
- Integer division can return either an integer or a real. Its result is not simply
  C integer division; an unresolved `NUMERIC` result has no interface representation.
  Zero divisors, `INT64_MIN / -1`, and `INT64_MIN % -1` need explicit handling.
- Shift counts and signed shifts follow Goat's checks and bit-pattern rules.
  Undefined or implementation-dependent C operations are not a substitute.
- Power always returns a real. Libm operations must retain the VM's domain and
  special-value behavior; purity alone does not establish numerical equivalence.

The native target must provide compatible `int64_t` and `double` semantics. A future
compiler/loader must verify its target and compiler options before enabling native
execution. This step does not claim that every C implementation satisfies that contract.

## Control flow and failures

Evaluation order must match the VM: binary operands are evaluated left to right;
call arguments are evaluated last to first, then the callee expression. Emit sequencing
explicitly rather than relying on C operand/argument order. Preserve short-circuiting, branch selection,
argument evaluation, normal returns and all observable exceptions. Reordering or
removing a pure expression can still change exceptions or termination.

The first body proof must exclude unsupported exceptional paths before choosing a
bare numeric C return. There is no native exception ABI in this step: potentially
throwing operations need a sufficient proof or rejection. They must never turn into
C undefined behavior, a fabricated numeric result, or a swallowed Goat exception.
Purity does not prove that a function returns normally or terminates. Resource limits
and recursive native-call integration remain obligations of the future runtime bridge.

## Cached decisions and diagnostics

`check_function_c_contract` writes `function_summary_t.c_support` and `c_blockers`.
It does not modify the shared AST's `NODE_FLAG_C_COMPATIBLE`, generate C, load a library,
or change VM execution.

| Blocker | Meaning |
| --- | --- |
| `analysis` | Direct-body information is not established or contains unsupported behavior |
| `parameters` | At least one parameter lacks an allowed fixed representation |
| `return` | The normal return lacks an allowed fixed representation |
| `effects` | Purity is not proven |
| `captures` | The body needs external data or a nonstatic callable binding |
| `body` | Expression/control/call semantics still need a complete body proof |

`unsupported` means a known interface type or capture is excluded by this initial
contract, not that equivalent C is impossible in principle. Insufficient type or effect
information yields `unknown`. Rechecking replaces stale results. `body` is always present
at this step, so passing every other check still yields `unknown`, never `supported`.
For example, numeric Fibonacci reaches `c=unknown` with `c-blockers=body`.

Collectors copy these decisions into existing function-summary events. Source tests
use `c-support` and `c-blockers` selectors with the same function location and signature
as `function` selectors. Blocker names use the order shown above, joined by `|`.

## Pointwise expression proof

`can_generate_c_code(node, value, context)` is also used during an isolated generic
function evaluation. Its context owns per-node representations for one signature;
ordinary analysis passes NULL and retains the existing shared-flag behavior.

Supported expressions are numeric literals/reads, parentheses, unary signs, numeric
addition/subtraction/multiplication, and all six numeric comparisons. Arithmetic
requires proven numeric operands, not just a numeric result. Lowering must use wrapping
integer helpers, explicit rounded integer conversions, and the exact comparison helpers
specified above. Boolean comparison/literal temporaries do not extend the numeric ABI.
No reassociation or direct overflowing signed C arithmetic is authorized by a proof.

A read uses its generic program-point type, including preceding writes and branch joins.
External captures start at TOP. Calls are unproven and conservatively forget state using
the existing generic evaluator. No concrete invocation or shared node flag is evidence.
Repeated observations intersect: incompatible representations or one failed proof leave
`unknown`. Unknown is distinct from an excluded interface type, and may be refined by
future analyses. Unsupported or unvisited expressions do not imply an eligible body.
In particular, constant folding cannot hide an unsupported operand behind a numeric result.

The arena-owned proof list is copied into collector snapshots. `c-expression` events carry
the function signature, source node and representation. They neither update the shared AST
flags nor clear `C_BLOCKER_BODY`. A variable read can have a representation even if the
statement producing it is unsupported; the later body checker must validate every required
statement and expression. Current absence of an exception ABI keeps division, modulo,
shifts, power, updates and calls unproven in this incremental step.
