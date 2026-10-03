# Native execution contract

This document describes the planned C backend and VM bridge. Neither C emission,
dynamic compilation/loading nor the `NATIVE` opcode is implemented yet. The
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
specializations. Node emission methods will receive that context in a later PR.

Replacement nodes preserve an original subtree and a simplified subtree. Native
generation must use the original subtree unless the replacement is justified for
the entire selected signature. A constant observed for `f(10)` cannot specialize
the implementation of `f(integer)` to that value. This does not change which
subtree ordinary bytecode generation uses.

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

Initially, the caller still executes ordinary `CALL`. It evaluates arguments and
the callee in Goat's existing order and establishes the normal function context.
The called function's bytecode starts with `NATIVE <descriptor-id>`, followed by
its ordinary bytecode body.

`NATIVE` selects an adapter using the actual function identity and exact runtime
types of the bound formal parameters. It must not dispatch by a variable's name
or coerce an integer to real to find an adapter. Aliases of the same function may
use its adapters; rebinding the variable to another function cannot reuse them.

There are three outcomes:

| Outcome | VM action |
| --- | --- |
| No matching ready adapter | Continue into the bytecode body with stack and context unchanged. |
| Native success | Box the numeric result and complete the call through normal `RET` semantics. |
| Controlled native resource limit | Discard the incomplete native result and restart the pure call in its bytecode body. |

On success, result ownership, stack cleanup and context restoration must match
ordinary bytecode return. On fallback, arguments and the callee are not evaluated
again. A retry must disable native entry for that call and its descendants until
it returns, otherwise recursion could repeatedly hit the same native limit.

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
| Function alias or rebinding | Dispatch by actual function identity. |
| Unsupported callee in a recursive group | Do not publish incomplete native callers. |
| Compiler/load/ABI failure | Keep ordinary execution available and identify the failing stage. |
| Native recursion limit | Retry in bytecode without repeating argument evaluation or native retries. |
| Analysis arena already freed | Dispatch and return without referring to AST storage. |

Cross-mode tests must compare values, runtime types, output and exceptions, and
also verify that successful native tests actually entered native code. Otherwise
a backend that always falls back could appear correct.
