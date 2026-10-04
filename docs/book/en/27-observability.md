# 27. Observability

[Contents](index.md) · [Русский](../ru/27-observability.md) · [Previous](26-separate-compilation.md) · [Next](28-testing.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-27-1"></a>

## 27.1. Several views of one program

The interpreter can show a program at several stages. Each output answers a different question: how text was parsed, what analysis established, which instructions were generated, and which execution path was actually used. Confusing these questions leads to false conclusions: a green function in a graph does not yet mean the processor executed its native version.

[launcher.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c) establishes the order: analysis, log, reconstructed source, graph, then C export or bytecode generation and execution. `--print-*` options usually add diagnostic output to ordinary execution rather than replacing it. C export and `--compile` are separate modes without body execution.

<a id="section-27-2"></a>

## 27.2. An example to inspect

File [27-observability.goat](../examples/27-observability.goat):

```goat
var value = 2;
if (false) { value = 100; }
const twice = func(x) { return x * 2; };
println(twice(value));
```

Its result is `4`. The branch containing `100` is unreachable, and `twice` admits an integer specialization. From the repository root:

```sh
./goat --optimize none --print-source-code docs/book/examples/27-observability.goat
./goat --print-source-code docs/book/examples/27-observability.goat
./goat --optimize none --print-bytecode docs/book/examples/27-observability.goat
```

Reconstructed source comes from the AST: comments and original whitespace are not restored. `none` preserves the original computation structure; `all` reveals chosen replacements and removed unreachable actions. This helps inspect tree transformations but does not prove two programs equivalent for every input.

Bytecode addresses are instruction indices, not byte offsets. Every fifth index is printed for readability. In the unoptimized example, `JIF` targets instruction 10, while `ARG 20` before `FUNC` gives the body start. `END` finishes top-level execution, but the buffer contains the body of `twice` after it. The complete reference is in [appendix C](appendix-c-vm-instructions.md).

<a id="section-27-3"></a>

## 27.3. The log: observations and proofs

```sh
./goat --print-analysis docs/book/examples/27-observability.goat
```

The log includes:

```text
#1 27-observability.goat, 1.1: write value = 2
#4 27-observability.goat, 3.15: summary x = 2
#27 27-observability.goat, 3.15: function-summary analyzed (integer) -> integer effects=none c=supported purity=pure direct-effects=none calls=no captures=[] c-blockers=none
```

`#` is the collector event sequence number, followed by filename, row, and column. `write` describes an observed assignment, `join` a state merge, and `summary` an accumulated declaration summary. This is an analyzer log, not a trace of actual VM execution.

The `x = 2` summary comes from a particular observed call. The `function-summary` line describes a separate proof for all values in the `(integer)` signature. It reports analysis status, result type, effects, purity, and C eligibility. `c-expression` entries describe expression points within a signature; `call-group` entries describe call-graph components. An observation without effects cannot substitute for proven purity of the whole specialization.

The collector and its text representation are in [collector.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/collector.c). Editing the program or changing passes may change event numbers; they are not persistent source identifiers.

<a id="section-27-4"></a>

## 27.4. The graph and its legend

Graph output requires installed Graphviz with `dot` available (`dot.exe` on Windows):

```sh
mkdir -p build/book-observe
./goat --save-graph build/book-observe/program.svg docs/book/examples/27-observability.goat
```

[visualization.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/visualization.c) builds DOT, invokes Graphviz, and removes the intermediate `.dot`. Supported extensions are `.svg` and `.png`.

| Appearance | Meaning |
|---|---|
| Light-gray outline and gray text | Unreachable node; no colored fill |
| `forestgreen` outline | C compatibility in the current view |
| Pale-green fill | Purity in the current view |
| Purple outline and fill | Replacement node with original and replacement branches |
| Dashed blue edge | Additional relation, such as a declaration reference |
| Dashed group outline | Scope |

A function may carry a `C view:` label identifying a selected signature. [function_summary.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/analysis/function_summary.c) selects the first pure `supported` specialization; the visualizer propagates its display properties through the reachable subtree. This is a display layer, not a mutation of general AST flags or a promise about other signatures. Read colors together with `C view` and the log.

> [!CAUTION]
> The graph shows one selected C specialization, not all variants simultaneously. Long string labels are shortened. A Graphviz error is printed to stderr but does not necessarily make Goat's final exit status unsuccessful: check the image's existence and the diagnostics.

<a id="section-27-5"></a>

## 27.5. Confirming actual native entry

```sh
./goat --native required --print-native docs/book/examples/27-observability.goat
```

With ordinary sufficient stack headroom, output after `4` is:

```text
mode=required
preparation=ready
bound=1
omitted=0
attempts=1
succeeded=1
retries=0
```

`supported` concerns analysis, `ready` preparation, and `succeeded=1` an actual successful adapter call. These are three different pieces of evidence. Chapter 25 explains counters and VM fallback.

Use files to keep logs separate from program output:

```sh
./goat --save-analysis build/book-observe/analysis.txt --native required --save-native build/book-observe/native.txt docs/book/examples/27-observability.goat
```

Only the program result remains on stdout. Errors still go to stderr. Options and prohibited combinations are collected in [appendix E](appendix-e-launch-interfaces.md); color and selected-view checks are in [check_graph.sh](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/scripts/check_graph.sh).
