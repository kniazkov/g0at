# Appendix G. Implementation Boundaries

[Contents](index.md) · [Русский](../ru/appendix-g-implementation-boundaries.md) · [Previous](appendix-f-implementation-map.md) · [Next](appendix-h-decision-history.md)

Updated for the implemented module system (#220); source baseline: [5b98271](https://github.com/kniazkov/g0at/tree/5b98271e26cb17bdc5589aaf827316016ba2e00a).

<a id="section-g-1"></a>

## G.1. Reading the matrix

“Yes” means a mechanism exists in the described revision, not support for every combination. The C column concerns general execution of a construct inside a specialization. Earlier constant folding can remove an unsupported operation; that does not add it to the native language. Built-ins already written in C are also distinct from generated specializations.

| Feature | Syntax | VM / model | Analysis | C generation |
| --- | --- | --- | --- | --- |
| Numeric literals | Yes | Integers and reals | Constants and types | Yes, within an eligible specialization |
| Strings / characters | Yes | Yes | Constants/domains | Supported scalar ABI and proven string operations |
| null | Yes | Yes | Constant/domain | No general null ABI value |
| Booleans | Yes | Yes | Constants/domain | Body values and ABI parameters/results |
| Locals and assignment | Yes | Yes | States, writes, joins | With one proven representation |
| Numeric +, −, * | Yes | Yes | Constants, intervals, types | Yes |
| Division, remainder, power | Yes | Yes | Abstract handlers | No general execution |
| Comparisons | Yes | Numbers, strings, bool; object equality | Precision depends on operands | Numeric |
| Logical and bitwise operations | Yes | Yes | Including short circuit | No general execution |
| ++ / -- | Yes | Yes | Modify state | Mutable numeric parameters/locals |
| if / else | Yes | Yes | Branch selection/merging | With proven condition and body |
| Functions, return | Yes | Yes | Calls and signatures | Proven pure scalar/string subset |
| Direct/mutual recursion | Through functions | Yes | SCCs and bounded fixed point | With proofs; resource fallback |
| Mutable-data capture | Yes | Closures | Conservative effect tracking | No |
| Capturing a const number | Yes | Yes | Capture tracking | No under current contract |
| Immutable function reference | Yes | Yes | Static-target resolution | With an eligible exact signature |
| Built-ins | Named call | Registry, including exit/spawn/join | Descriptor and abstract handler | Proven numeric/math, scalar conversion and string built-ins; not arbitrary built-in calls |
| try / catch / throw | Yes | Yes | Limited model; not complete exception flow | No |
| Block object | Yes | Context object | Limited information | Not as a general object value |
| Properties and prototypes | Yes | Named/indexed access, provenance and access checks | Proven denial rejected; unknown checked at runtime | No unchecked general object access |
| Arrays | Yes | Literals, indexing, methods, shallow copying | Conservative domains | No general array ABI |
| for | Yes | Scoped loop, nested statements | Widened invariant; bounded passes | Proven numeric bodies; resource fallback |
| while, do/while, break, continue | Yes | Loops and structured unwinding | Conservative control-flow analysis | Only proven eligible bodies |
| include / import | Yes | Persistent file scopes and verified module graph | Metadata-only dependency analysis | Validated per-call native specializations |
| User threads | `spawn` / `join` builtins | OS-backed `thread_t`; equal lifetime, serialized initialization | Dynamic initialization restrictions checked at runtime | Shared invocation/provenance rules |

<a id="section-g-2"></a>

## G.2. Limits that “supported” must not hide

> [!CAUTION]
> Analysis bounds depth, specialization counts, and iterations. Exhausting a limit loses a proof rather than authorizing an optimistic assumption. Variable relationships and branch refinement from arbitrary conditions do not form a complete relational analysis. Exceptions execute more precisely in the VM than the analyzer models them.

> [!CAUTION]
> The scalar native ABI admits int64, double, boolean, character and string values; it has no general array/object/function representation. Generation requires proven eligible pure bodies and supported operations. Building native code requires a compatible external compiler; executing a ready archive does not. Linux/Windows loaders are supported. On the standalone query-ABI path, depth and stack limits can return a pure compiled function to the VM; ZIP-module adapters are never replayed after native entry. Libraries execute in-process without isolation.

> [!CAUTION]
> Lexical-error handling has known incomplete cases described in chapter 5. The binary loader checks structure and checksums without proving untrusted bytecode safe. Windows module paths use the shared Unicode filesystem/process layer; external tools must still support the selected target. A successful numeric example must not hide these points.

<a id="section-g-3"></a>

## G.3. Where to establish status

Check syntax in [parser.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/parser/parser.c); executable operations in [vm.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/vm/vm.c) and [object.h](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/model/object.h); abstract facts in [lattice.h](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/analysis/lattice.h), [function_call.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/analysis/function_call.c), and [c_body.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/analysis/c_body.c); the native boundary in [native_abi.h](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/include/goat/native_abi.h) and [c_generation.c](https://github.com/kniazkov/g0at/blob/5b98271e26cb17bdc5589aaf827316016ba2e00a/src/codegen/c_generation.c).

A practical route is to reproduce a program in the VM, inspect the relevant signature's log, inspect generated C, and only then read the execution report. Support at one step does not prove the next. Chapters 5–7, 9, 16–20, and 22–26 give the matrix's detailed foundations.

## G.4. Implemented delivery versus unmeasured portability

The [delivery matrix](../../module-delivery-tests.md) records actual native-call counters,
relocated Debug/Release execution, distinct MINGW64/UCRT64 DLLs and lower-runtime controls.
The control modules execute on glibc 2.35 (Ubuntu 22.04) or 2.36 (Debian Bookworm),
according to their actual symbol requirements. Ordinary declared glibc 2.14/2.17 floors
have **not** been executed on those exact runtimes; actual Windows 10 floor execution
also remains an explicit validation plan, not a missing loader implementation or a passed test.
ARM `host+sha2` / `linux-aarch64-gnu+sha2` tests SHA1/SHA2, not PMULL or all `crypto` features.

Native libraries are trusted in-process code. Inspect's isolated crash/timeout probe is
not a security sandbox. A detected ABI violation terminates the process without replay.
ZIP64 is unsupported; checksums establish integrity, not publisher authenticity. The
module compiler deliberately retains conservative analysis rather than promising every
single-file AST optimization. These limits are not erased by completion of the checklist.
