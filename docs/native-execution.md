# Native execution contract

This document describes the C backend and planned VM bridge. Whole-module numeric C emission, library compilation/loading and optional native
dispatch through function objects are implemented. Automatic pipeline integration
and bounded native recursion remain for step 17. The
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

The emitters support numeric literals, parameter reads, parentheses, unary signs,
binary addition, subtraction and multiplication, and all six numeric comparisons.
Bodies support statement sequences, nested blocks, `if/else`, expression statements
and early returns. Conditions accept numeric and boolean expressions: NaN is true,
and either signed zero is false. Boolean temporaries are not function interface types.
Generated parameter names use declaration identity, so Goat names need not be valid C identifiers. Function names are backend
ASCII identifiers prefixed with `goat_`. Unsupported operators/control flow still
report `C_GENERATION_UNSUPPORTED`, even when analysis proves C eligibility.
Replacement nodes delegate C lowering to their original children. Branch generation
uses signature-scoped expression proofs, not shared reachability flags. Each
condition's setup runs once before its `if`; branch setup stays inside that branch.
Termination is tracked through node emitters; generation rejects an uncovered
fallthrough instead of inventing a return value. Literal truth may establish that
a return is unconditional, without reusing concrete-call observations.

Each successful result is standalone C11 source with the required standard headers.
Integers use `INT64_C` with a safe spelling for `INT64_MIN`. Finite doubles use exact
hexadecimal literals; negative zero, subnormals, NaN and infinities are covered.
The target requires binary64 doubles. Integer arithmetic wraps modulo 2^64 using
unsigned operations and a range-safe conversion back to int64_t. Generated integer
operations call `goat_i64_add`, `goat_i64_sub`, `goat_i64_mul` and `goat_i64_neg`:
small `static inline` functions sharing `goat_i64_bits`. Only used helpers are emitted.
Unary plus needs no helper. Operands remain explicitly sequenced in temporaries;
function-call argument evaluation order in C is not used to order Goat operations. Mixed operands
convert to double before arithmetic. Ordered `goat_tN` temporaries evaluate each
operand once, left to right; volatile double temporaries round conversions and
each result, preventing excess precision and multiply-add contraction across
operations. Fast-math compilation is rejected. Implicit numeric return conversion
is not emitted.

Mixed integer/real comparisons preserve the integer exactly: the helper checks
NaN and the int64 range before casting, compares the truncated whole part, then
uses the fractional part to distinguish equality. It never rounds a large integer
to double. Unordered comparisons are false except for `!=`. Logical `!`, `!!`,
`&&` and `||` are not lowered yet.

Local variables and constants have one int64, double or bool representation per
signature. `goat_lN` names identify declarations, including shadowed names. Storage
is reserved at the start of the corresponding Goat scope (function or braced
block); initializers still run in source order at the declaration site. An `if`
without braces does not introduce a Goat scope. Constant storage is initialized
at its declaration site too; assignment lowering rejects writes to it rather
than relying on a C `const` qualifier. Double storage is volatile to retain binary64
rounding on writes. Assignment expressions snapshot their result into a temporary
before later operands can change the destination. Uninitialized declarations,
representation changes and captured storage remain outside this subset.

Static user-function calls use the selected caller summary's `c_calls` records
and exact function identity plus parameter types. Immutable aliases are accepted;
source names do not identify built-ins. Callee bindings supply backend names and
validated numeric prototypes, so the callee definition may follow its caller.
Missing or ambiguous proofs, missing bindings and incompatible representations
fail the whole function; no argument coercion or dynamic dispatch is invented.

Goat evaluates call arguments right to left, then evaluates the callee. Each
argument is captured in that order before the C call, which receives the required
parameters in their original positions. Extra arguments are evaluated and discarded;
missing numeric arguments are rejected. Static callee resolution is effect-free,
so no runtime function-object lookup is emitted. The call result is captured once,
with binary64 rounding for doubles. Built-in and mutable/dynamic calls still lack
C lowering. Direct and mutual recursion use the same typed calls and prototypes,
including recursive transitions between numeric specializations. Self-bindings
must agree with the current definition's name and return type. Standalone tests
compile and execute Fibonacci, factorial, two- and three-function cycles, recursive
aliases, mixed-type cycles and ignored arguments with local side effects. They
exercise inputs beyond the concrete analysis seeds, integer wrapping and binary64
rounding. Complete-module assembly retains only successfully lowered dependency closures.

These standalone recursion tests use bounded depths. The CLI does not attach native
descriptors yet; host-stack limits and controlled bytecode retry remain prerequisites
for enabling recursive adapters through the pipeline.

`scripts/check_c_generation.sh UNIT_BINARY OUTPUT_DIR` generates test source,
compiles it with `${CC:-gcc}` and executes numeric assertions at `-O2`. CI runs it
with Linux GCC/Clang and all Windows GCC targets, plus Linux sanitizers and GCC
x87 evaluation. Arithmetic checks compare all numeric type pairs with the object
model at boundary values, including signed zeros, NaN and infinities. CI preserves generated source
as an artifact. The unit executable's `--emit-c-tests` is test-only.
`scripts/check_c_module.sh GOAT_BINARY OUTPUT_DIR` additionally exercises the public
CLI and compiles/executes its exported modules on every CI compiler target.

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

## Module inventory

`create_c_module` builds a compilation-time inventory from the original AST and
already proven numeric summaries. Functions receive preorder ordinals; signatures
are sorted by parameter count and lattice type order. Module entry IDs and C names
are deterministic for the same tree/signatures, independent of AST node numbers,
addresses and signature registration order. They are not persistent identifiers
across source edits. Names encode a function ordinal and integer/real parameters.

Generic C body analysis now retains exact call-site targets in `c_calls`. The
inventory uses these records rather than the broader may-call graph. It deduplicates
repeated observations, supplies callee names to the generation context and keeps
self/mutual-recursive dependencies. A missing or ambiguous target blocks the caller;
blocking propagates to callers until stable, leaving independent entries available.
Availability proves dependency closure only, not emitter or runtime readiness.

Inventory storage and names belong to the supplied arena; AST and summary pointers
are borrowed. Rebuild the inventory after reanalysis. Summary snapshots copy call
records but borrow target identities, whose parameter keys remain immutable.
The inventory does not compile code or enable VM dispatch.

The remaining stages are listed in the [implementation roadmap](native-roadmap.md).

## Generated module

`generate_c_module` first emits each candidate definition transactionally, then
propagates backend failures through exact dependencies until stable. Successful
independent functions survive a failed recursive group. Neither inventory
availability nor analysis proofs are modified. Omission records belong to the
supplied arena and distinguish direct emission failures from blocked dependencies;
source text is separately owned by the caller. Counts refer to inventory candidates,
not all function declarations in the program.

Output has shared headers/helpers once, all retained prototypes, then definitions
in inventory order. It contains no addresses, timestamps or input filenames.
Zero retained functions still yields a valid C11 translation unit. Assembly adds numeric adapters and an immutable module descriptor for the
[version 1 ABI](native-abi.md).

`--print-c` and `--save-c` select source-only export after analysis and before
bytecode generation: the Goat program is not executed. Both require `--optimize all`
and reject other `--print-*` modes, keeping stdout usable as C. Graph and analysis
sidecar files remain available. `--save-c` takes no argument: it replaces the input
extension with `.c` in the same directory; the filename helper uses `generated.c`
when no source filename exists. The current CLI still requires an input file.
An input already ending in `.c` (case-insensitive) is rejected for saving to avoid
overwriting it. Printing that input is allowed. Existing output files are replaced.
Backend omissions are reported on stderr; a successfully written partial or empty
module is a successful export. Parse and I/O errors fail the command.

One C source file contains the selected specializations, forward declarations,
numerical helpers and VM adapters. Use module-wide specialization identifiers;
AST node numbers are not globally unique. The module includes all static native
dependencies, including complete recursive groups. A missing dependency prevents
emission of its callers, without preventing unrelated functions from being emitted.

Emit a specialization completely or reject it. Never expose a partial function
or silently call back into arbitrary Goat bytecode from generated C. Generated
calls use typed C signatures; the VM uses a uniform, versioned adapter interface.
The exact layout and status rules are specified in the [numeric ABI](native-abi.md).

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

Linux and Windows library compilation is implemented by `--save-library`; see the
[compilation contract](native-compilation.md). Loading and optional VM dispatch are available through internal APIs; the CLI does
not connect the full pipeline yet.

## VM call and fallback

The caller uses ordinary `CALL argc`, with the existing `uint16_t` argument count
and stack convention. Arguments are evaluated right to left, then the callee;
the function object is on top of the stack, followed by the first argument. No
`NATIVE` opcode is introduced. `CALL` delegates to the object's call method as it
does today; native selection belongs to the implementation of the function object.

Function-creation metadata in the bytecode associates the parameter names and
bytecode entry with an optional native descriptor reference. `FUNC` transfers
that association into the created function object. Keep the descriptor reference
separate from the parameter-name array. The in-memory `bytecode_t.native_functions` table is indexed by the instruction
position of `FUNC` and allocated only on the first binding.
`bind_bytecode_native_function` validates the opcode and formal arity, then retains
the descriptor; NULL removes the association. Bindings are established before
execution. The table is separate from serialized `buffer` bytes: the file format,
`ARG`/`FUNC` instruction operands, and parameter-name arrays remain unchanged.
`FUNC` retains the selected descriptor in each newly created function object.
`free_bytecode` releases table references; object DECREF, tracing GC and process
destruction release object references independently. The binder checks structure,
not semantic equivalence: its caller must associate the correct function definition
and only adapters safe to execute, including any recursion constraints.

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

The final contract has three outcomes:

| Outcome | Function call action |
| --- | --- |
| No matching ready adapter | Create the ordinary call context, bind parameters and enter the bytecode body. |
| Native success | Consume the arguments, box and push the result, and continue after `CALL`, without creating a call context. |
| Controlled native resource limit | Discard the incomplete native result and enter the ordinary bytecode call path with the original arguments. |

Step 16 implements exact selection, native success and unmatched-signature fallback.
The adapter receives only formal numeric arguments, with zeroed reserved fields.
An integral-valued real remains real; booleans and convertible strings never match.
A successful result must have the declared return tag and a zero reserved field.
All nonzero adapter statuses currently stop `run` with a nonzero return and are
preserved in `thread.native_status`; malformed success becomes `BAD_REQUEST`.
They are backend failures, not catchable Goat exceptions, and do not trigger a
retry. `RESOURCE_LIMIT` retry is intentionally deferred to step 17, together with
bounded recursion and suppression of native entry in fallback descendants.

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

These are acceptance gates for the complete pipeline:

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


Step 16 tests run with `scripts/check_native_call.sh GOAT UNIT_BINARY OUTPUT_DIR`.
They parse source, generate bytecode, bind loaded descriptors and free the analysis
arena before execution. A counting provider verifies actual native entry; a generated
provider is also exercised with bytecode bodies disabled. Tests cover all ordered
numeric signatures, extra/missing arguments, aliases/rebinding, builtins, backend
failures, numeric edges, unchanged contexts, and library unload through DECREF,
unreachable closure-cycle collection and process destruction. They run on Linux
GCC/Clang and all three Windows targets.
