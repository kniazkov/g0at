# Native execution contract

This document describes the planned C backend and VM bridge. Neither C emission,
dynamic compilation/loading nor native dispatch for compiled Goat functions is
implemented yet. The
[C subset contract](c-subset.md) defines the numerical semantics and the proofs
already produced by the analyzer; this document defines how a backend may use them.

## Generation unit and specialization

The smallest generated unit is a complete function specialization, never an
isolated expression or branch. A specialization is identified by the function's
identity and the ordered, normalized types of its formal parameters. For example,
`f(integer)` and `f(real)` produce distinct C functions, even when the real argument
is `10.0`. Constants and integer ranges do not create value-specific native code.

Each parameter and the normal return must have a fixed representation: `int64_t`
or `double`. Boolean temporaries are allowed; boolean interfaces and undetermined
numeric unions are not. Missing arguments have Goat's `null` type. Extra arguments
are evaluated normally but do not belong to the specialization key. Zero-parameter
functions are allowed when the remaining requirements hold.

Eligibility requires proven transitive purity, no captured data, and a complete
body proof for this signature. Immutable references to known functions may identify
static callees; they are not a general closure ABI. Unsupported exceptional paths,
dynamic calls and object operations stay in the VM. Purity alone proves neither
termination nor the absence of exceptions.

## Proof is not executable code

Keep these decisions separate:

| Decision | Meaning |
| --- | --- |
| Analyzer eligibility | The specialization satisfies the C subset contract. |
| Emitter support | The current emitter can lower every required construct and callee. |
| Module readiness | Compilation, loading and ABI validation succeeded. |
| Call selection | The actual function and parameter types match an available adapter. |

An analyzer result of `c=supported` or a green graph is not permission to jump into
native code. An emitter limitation or compiler failure must not rewrite the
analyzer's proof as `unsupported`. Report the failing stage separately.

Generation uses the specialization's generic expression and body proofs, not
shared node flags or concrete values observed at one call. Its context must carry
the signature, result and expression representations, C bindings and known callee
specializations. Node emission methods now receive a `c_generation_context_t`.
`generate_c_function` is the external entry point: it accepts one proven function
summary and borrowed C names/bindings/callees, and returns either complete source
or a failure status and the first failing node. Node-level helpers are internal
lowering operations, not independent compilation units.

Expression lowering returns a typed value expression and an optional ordered
prelude of statements. The caller must place that prelude at the expression's
evaluation point (inside the appropriate branch for conditional evaluation),
then use the value. Release both with `destroy_c_expression`. Statement lowering
returns success explicitly. A failed function attempt discards all accumulated
source without changing the analyzer's summary.

Concrete emitters are still absent; even a proven function currently reports
`C_GENERATION_UNSUPPORTED`. Replacement nodes delegate C lowering to their
original children, including nested replacements.

Replacement nodes preserve an original subtree and a simplified subtree. Native
generation currently always uses the original subtree. Reusing a simplified
subtree would require a separate proof for the entire selected signature. A constant observed for `f(10)` cannot specialize
the implementation of `f(integer)` to that value. This does not change which
subtree ordinary bytecode generation uses. Expression-type lookup unwraps the
same original nodes to find the selected summary's pre-rewrite proofs. Missing
proofs stay unknown; neither a replacement literal nor shared flags fill the gap.
Lowering does not restore or mutate the AST, its parents, flags or proof records.
Statement replacements retain the original control structure for C lowering,
including branches absent from the executable bytecode subtree.

## Generated module

One C source file contains the selected specializations, forward declarations,
numerical helpers and VM adapters. Use module-wide specialization identifiers;
AST node numbers are not globally unique. The module includes all static native
dependencies, including complete recursive groups. A missing dependency prevents
emission of its callers, without preventing unrelated functions from being emitted.

Emit a specialization completely or reject it. Never expose a partial function
or silently call back into arbitrary Goat bytecode from generated C. Generated
calls use typed C signatures; the VM uses a uniform, versioned adapter interface.
The exact ABI layout belongs to the adapter implementation step, not this contract.

The ABI exchanges numeric values and an explicit execution status. It does not
expose internal `object_t` layouts or pointers into the analysis arena. Runtime
descriptors own the information needed after the AST has been released. Validate
ABI and target compatibility before making adapters callable, and retain a loaded
library for as long as any VM descriptor may refer to it.

The numerical and evaluation-order rules in the C subset contract are mandatory,
including wrapping integers, exact mixed comparisons, floating-point special
values and explicit sequencing. Compiler settings must preserve these rules.
A C compiler is required only when native compilation is requested; ordinary
interpretation must remain dependency-free.

## VM call and fallback

The caller uses ordinary `CALL argc`, with the existing `uint16_t` argument count
and stack convention. Arguments are evaluated right to left, then the callee;
the function object is on top of the stack, followed by the first argument. No
`NATIVE` opcode is introduced. `CALL` delegates to the object's call method as it
does today; native selection belongs to the implementation of the function object.

Function-creation metadata in the bytecode associates the parameter names and
bytecode entry with an optional native descriptor reference. `FUNC` transfers
that association into the created function object. Keep the descriptor reference
separate from the parameter-name array. Its exact encoding is deferred to the
implementation step; bytecode must not contain raw loaded-library addresses.
The loader resolves descriptors to validated adapters.

The function object contains everything needed to choose between native and
bytecode execution. Its optional descriptor maps ordered formal-parameter types
to adapters and may be shared by objects created from the same function definition.
It follows the runtime ownership rules above, independently of the AST arena.

Before creating a call context or binding parameter names, the function's call
method inspects the already evaluated arguments and selects an exact runtime-type
match. Missing arguments count as `null`; extras do not affect selection. It must
not dispatch by a variable's name or coerce an integer to real to find an adapter.
Aliases use the actual object's descriptor; rebinding a variable selects the new
object's call behavior. Built-in functions retain their existing call path.

There are three outcomes:

| Outcome | Function call action |
| --- | --- |
| No matching ready adapter | Create the ordinary call context, bind parameters and enter the bytecode body. |
| Native success | Consume the arguments, box and push the result, and continue after `CALL`, without creating a call context. |
| Controlled native resource limit | Discard the incomplete native result and enter the ordinary bytecode call path with the original arguments. |

On native success, stack balance and result ownership must match an ordinary
completed call, including disposal of extra arguments. The caller's context stays
unchanged; no `RET` instruction or callee-context restoration is needed. Keep the
function object and arguments alive for the attempt, and preserve the arguments
until native success is committed or bytecode fallback takes ownership.

On fallback, arguments and the callee are not evaluated again. A resource-limit
retry must disable native entry for that call and its descendants until it returns,
otherwise recursion could repeatedly hit the same native limit.

The resource limit must be checked before exhausting the host C stack. A crash,
undefined behavior or corrupted state is not a recoverable retry signal. Retrying
is valid only for proven pure execution without observable partial effects. Native
resource handling is a prerequisite for enabling recursive code in the VM, even
if earlier standalone emitter tests already compile recursive functions.

Build/load failures leave the bytecode path available. Explicit diagnostic modes
may instead fail with a clear error when requested native execution cannot be
provided. The later CLI step will define that policy; this PR adds no options.
The planned initial default keeps native execution disabled. Source inspection
and saving must also work independently of compiling or loading a library.

## Acceptance cases for later steps

These are implementation gates, not claims about tests that exist today:

| Case | Required behavior |
| --- | --- |
| `f(10)` followed by `f(11)` | The integer specialization works for both values. |
| `f(10)` and `f(10.0)` | Select distinct integer and real signatures, or fall back for an unavailable one. |
| Missing numeric argument | Preserve `null` semantics through bytecode fallback. |
| Extra argument with a side effect | Evaluate it exactly once, on both native success and fallback. |
| Native success | No callee context allocation; arguments are consumed once and one result replaces the call. |
| No descriptor or unmatched signature | Ordinary bytecode call with the original arguments. |
| Function alias or rebinding | Dispatch by actual function identity. |
| Unsupported callee in a recursive group | Do not publish incomplete native callers. |
| Compiler/load/ABI failure | Keep ordinary execution available and identify the failing stage. |
| Native recursion limit | Retry in bytecode without repeating argument evaluation or native retries. |
| Analysis arena already freed | Dispatch and return without referring to AST storage. |

Cross-mode tests must compare values, runtime types, output and exceptions, and
also verify that successful native tests actually entered native code. Otherwise
a backend that always falls back could appear correct.
