# Implementation map and lifetimes

[Contents](index.md) · [Русский](../ru/architecture.md)

## Follow one request

`src/main.c` handles internal cleanup-service mode before ordinary CLI parsing.
`src/cli/launcher.c` then chooses interactive input, module input, an already
compiled binary, or standalone source. These paths share components but are not
one interchangeable pipeline: loading an archive must validate stored evidence,
whereas compiling source can create new evidence from an AST.

For standalone source, go() fixes the canonical launch path. The launcher reads
UTF-8, creates a scanner, groups brackets, reduces tokens, builds a root, and
calls analyze(). Diagnostics above warning severity prevent generation. It can
then emit a graph, regenerated Goat, C, a compiled binary, or execute bytecode.
Deferred function bodies are emitted after their enclosing code has established
entry references. Native preparation happens before the analysis arena dies.

## Directory map

| Directory | Responsibility and next guide |
| --- | --- |
| `src/cli`, `src/main.c` | Input mode, options, diagnostics and launch lifetime; this page |
| `src/scanner`, `src/parser`, `src/graph` | [Tokens, binding and AST protocols](front-end.md) |
| `src/analysis` | [Values and state](abstract-values.md), [functions](functions.md), [branches](static-analysis.md) |
| `src/codegen` | Bytecode builders and [native generation](native.md) |
| `src/vm`, `src/model` | [Execution, objects, exceptions and collection](execution.md); module lifecycle below |
| `src/common`, `src/module` | [Source graph, module formats and publication](modules.md) |
| `src/builtins`, `src/native`, `include/goat` | Built-in descriptors and [native ABI](native.md) |
| `lib` | [Portable facilities and ownership](platform.md) |
| `src/resources`, `src/inspect.c`, `src/merge.c`, `tools` | [Diagnostics and developer tools](tools.md) |
| `test/src`, `test/support`, `test/data` | [Oracles, runners and fixtures](testing.md) |
| `scripts`, `test/*.cmake`, `.github/workflows` | [Local and CI verification](testing.md) |

Third-party implementations and generated tables have their own provenance;
editing every generated declaration is not part of maintaining explanatory
comments. See [maintenance](maintenance.md) for how a change reaches these guides.

## Know which owner keeps a pointer alive

| Data | Owner / release boundary | Common mistake |
| --- | --- | --- |
| Scanner and tokens | `parser_memory_t.tokens` | Keeping a token pointer after parsing |
| AST, scopes, analysis facts | `parser_memory_t.graph` | Saving AST addresses in runtime metadata |
| Positions and errors | Separate compilation arenas | Destroying diagnostics before rendering them |
| Linked bytecode | `bytecode_t`, free_bytecode() | Treating a borrowed image as an owned allocation |
| Runtime values | Process model, retained references and GC | Holding a C pointer without the required root |
| Module graph | Snapshot retained for the whole execution | Releasing immutable images before image-backed objects |
| Native entry address | Function/library descriptor lifetime | Unloading a library while a function still refers to it |
| Child process and work directory | Explicit run/scenario owner | Treating command success as proof of completed cleanup |

The launcher can release token storage before analysis, and AST/source storage
before ordinary VM execution. This is possible because generated instructions,
constant data and runtime descriptors no longer depend on those compilation
allocations. Module compilation's optional backend likewise must finish while
source graphs are alive; it may not retain graph pointers.

## Locate a failure by the boundary it crossed

A parse diagnostic belongs to source construction; a missing analysis proof is
not automatically a runtime error. An invalid archive is a loader failure, not
an absent module. A native backend failure is different from a program throwing
an exception. A successful scenario whose directory cannot be removed is still
a failed test. Preserve these distinctions in messages and tests.
