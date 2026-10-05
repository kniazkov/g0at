# Appendix G. Implementation Boundaries

[Contents](index.md) · [Русский](../ru/appendix-g-implementation-boundaries.md) · [Previous](appendix-f-implementation-map.md) · [Next](appendix-h-decision-history.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-g-1"></a>

## G.1. Reading the matrix

“Yes” means a mechanism exists in the described revision, not support for every combination. The C column concerns general execution of a construct inside a specialization. Earlier constant folding can remove an unsupported operation; that does not add it to the native language. Built-ins already written in C are also distinct from generated specializations.

| Feature | Syntax | VM / model | Analysis | C generation |
| --- | --- | --- | --- | --- |
| Numeric literals | Yes | Integers and reals | Constants and types | Yes, within an eligible specialization |
| Strings, null | Yes | Yes | Constants/domains | No general execution |
| Booleans | Yes | Yes | Constants/domain | Inside a body; not ABI parameters/results |
| Locals and assignment | Yes | Yes | States, writes, joins | With one proven representation |
| Numeric +, −, * | Yes | Yes | Constants, intervals, types | Yes |
| Division, remainder, power | Yes | Yes | Abstract handlers | No general execution |
| Comparisons | Yes | Numbers, strings, bool; object equality | Precision depends on operands | Numeric |
| Logical and bitwise operations | Yes | Yes | Including short circuit | No general execution |
| ++ / -- | Yes | Yes | Modify state | Mutable numeric parameters/locals |
| if / else | Yes | Yes | Branch selection/merging | With proven condition and body |
| Functions, return | Yes | Yes | Calls and signatures | Pure numeric subset |
| Direct/mutual recursion | Through functions | Yes | SCCs and bounded fixed point | With proofs; resource fallback |
| Mutable-data capture | Yes | Closures | Conservative effect tracking | No |
| Capturing a const number | Yes | Yes | Capture tracking | No under current contract |
| Immutable function reference | Yes | Yes | Static-target resolution | With an eligible exact signature |
| Built-ins | Named call | 33 implementations | Descriptor and abstract handler | No general calls; folding is separate |
| try / catch / throw | Yes | Yes | Limited model; not complete exception flow | No |
| Block object | Yes | Context object | Limited information | Not as a general object value |
| Arbitrary properties and prototypes | No general access | Model C API exists | Not equivalent to syntax support | No |
| Arrays | No | No complete user-facing path | Domain element exists | No |
| for | Yes | Scoped loop, nested statements | Widened invariant; bounded passes | Proven numeric bodies; resource fallback |
| while, do/while, break, continue, imports | No | No path from such source syntax | No complete mechanism | No |
| User threads | No | Internal process/thread structures exist | Not a user-threading model | No interface |

<a id="section-g-2"></a>

## G.2. Limits that “supported” must not hide

> [!CAUTION]
> Analysis bounds depth, specialization counts, and iterations. Exhausting a limit loses a proof rather than authorizing an optimistic assumption. Variable relationships and branch refinement from arbitrary conditions do not form a complete relational analysis. Exceptions execute more precisely in the VM than the analyzer models them.

> [!CAUTION]
> The native ABI is limited to int64/double, and generation to proven pure bodies. Preparation requires a compatible external compiler and Linux/Windows loader. Depth, call-budget, and stack limits can return even a successfully compiled function to the VM. Libraries execute in-process without isolation.

> [!CAUTION]
> Lexical-error handling has known incomplete cases described in chapter 5. The binary loader checks structure and checksums without proving untrusted bytecode safe. Windows retains ANSI-API limitations for paths and diagnostics. A successful numeric example must not hide these points.

<a id="section-g-3"></a>

## G.3. Where to establish status

Check syntax in [parser.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/parser/parser.c); executable operations in [vm.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/vm.c) and [object.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/object.h); abstract facts in [lattice.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/lattice.h), [function_call.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/function_call.c), and [c_body.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/c_body.c); the native boundary in [native_abi.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/include/goat/native_abi.h) and [c_generation.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/codegen/c_generation.c).

A practical route is to reproduce a program in the VM, inspect the relevant signature's log, inspect generated C, and only then read the execution report. Support at one step does not prove the next. Chapters 5–7, 9, 16–20, and 22–26 give the matrix's detailed foundations.
