# 21. AST Transformations and Semantic Preservation

[Contents](index.md) · [Русский](../ru/21-ast-transformations.md) · [Previous chapter](20-native-eligibility.md) · [Next chapter](22-c-code-generation.md)

Revision 8. Implementation described: [commit b91b405](https://github.com/kniazkov/g0at/tree/b91b405e949cc93d4aa9d243be622c5b143cb858).

<a id="section-21-1"></a>

## 21.1. A proof becomes a program change

Analysis may know that `2 + 3` equals `5`. Optimization takes the next step: replacing the expression with a number node. This changes program representation, so a pleasing log result is insufficient. Return values, effect order, exceptions, and possible nontermination must be preserved.

In [analysis.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/analysis/analysis.c), transformations follow function analysis, reachability, and property classification. With `--optimize none`, analysis ends after name binding; these optimizing passes do not run. Comparing the two modes therefore exposes the optimizer's changes.

The shared AST still represents the program for all its valid calls. Properties of one native specialization do not permit changing it as though other types or arguments did not exist.

<a id="section-21-2"></a>

## 21.2. An unreachable path and an unnecessary computation

[reachability.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/analysis/reachability.c) marks subtrees that do not execute on a proven path. In an `if` with known truthiness, one branch can disappear from bytecode. But evaluating the condition itself cannot always be removed.

If a condition prints a line and returns false, falsity does not cancel the print. The generator in [if_else.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/graph/if_else.c) can retain a nonliteral condition's evaluation, discard its result with `POP`, and keep only the chosen branch. For `ABSTRACT_NEVER`, evaluating the condition itself remains: it has no normal continuation.

Replacing the entire `if` with its chosen branch requires something stronger: the condition must be safely discardable. `can_discard_expression` is deliberately separate from knowing truthiness.

<a id="section-21-3"></a>

## 21.3. Constant folding

[simplification.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/analysis/simplification.c) visits children before parents. If a scalar expression has a proven value and its computation can be replaced, a corresponding literal is created. Integer and real values go directly to node constructors, without printing and reparsing.

Purity and subtree composition are checked. Calls, function creation, and object blocks are not accepted as arbitrarily discardable scalar operations. A call can throw or diverge even if all its normal returns produce one constant.

Deferred function bodies have a safe special case: a closed expression consisting only of literals can be evaluated independently of parameters. But `x + 1`, which produced `5` in an investigated call, cannot become `5` in the shared body.

If no constant is proven or the path has ended, no literal is created. Optimization must be able to leave an expression intact—an ordinary check outcome, rather than a reason to guess a value.

<a id="section-21-4"></a>

## 21.4. The original does not disappear

A replacement is stored in a special node from [replacement.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/graph/replacement.c). It links the original and resulting representations. Execution and generation use the result; the original structure remains available for restoration and history display.

This is not a deep copy of the whole tree: subtrees may be shared. Parent links follow the executable structure, and historical links must not be treated as additional ownership of independent node copies. The arena still owns memory.

Before another analysis, `restore_graph` puts originals back in place of replacements and repairs parent links. Previous facts are then reset, and scopes and summaries are rebuilt. Otherwise, reanalysis could mistake an earlier optimization result for the original program and lose alternative paths.

[test_replacement.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/test/test_replacement.c) checks these contracts, including restoration. A correct first-run result does not prove repeated processing of the same graph is correct.

<a id="section-21-5"></a>

## 21.5. Facts of one specialization

C checks have their own pointwise proofs. These concern specific parameter types and live in a summary, rather than unconditionally replacing the shared body. `c_expression_constant` returns a constant only when its representation is known and its evaluation is discardable.

For example, comparing two integer expressions may have a proven result inside one specialization. The generator may use this when emitting that specialization's C code. It does not follow that the source function should lose a VM branch for other valid arguments.

Real-constant equality for this proof accounts for signed zero. `NaN` fails ordinary `a == b`, so this mechanism does not retain it as an agreed constant. Numeric representation matters alongside mathematical magnitude.

> [!CAUTION]
> General folding is deliberately restricted and does not execute arbitrary user functions during compilation. A missing flag or unfolded expression does not mean optimization is impossible in principle; it means the current mechanism did not obtain the required permission.

<a id="section-21-6"></a>

## 21.6. A program whose behavior must survive

File [21-preservation.goat](../examples/21-preservation.goat):

```goat
if (1 + 2 == 3) { println("selected"); } else { println("dead"); }
const signal = func() { println("effect"); return 0; };
if (signal()) { println("unselected"); }
const step = func(x) {
    if (x < 0) { return 0; }
    return x + 1;
};
println(step(4));
println(step(-4));
var count = 0;
const pair = func(a, b) { return a * 10 + b; };
println(pair(count++, count++));
try { println(1 / 0); } catch (error) { println(error); }
println(-0.0);
```

Both optimization modes produce:

```text
selected
effect
5
0
10
DIVISION_BY_ZERO
-0
```

The first condition is computed from literals; the `dead` branch is unnecessary. Calling `signal` must preserve `effect`. This user call receives no global constant result in the reachability pass, so the fact that it returns zero does not permit its removal.

Two calls to `step` check that shared code has not become specialized for the first argument, `4`. `pair` checks right-to-left argument evaluation: its first parameter receives `1`, its second `0`. The division-by-zero handler confirms that optimization preserves the exception. The final line shows the current output format for negative real zero: `-0`.

<a id="section-21-7"></a>

## 21.7. Comparing representations

From the repository root, reconstructed source can be inspected in both modes:

```sh
./goat --optimize none --print-source-code docs/book/examples/21-preservation.goat
./goat --optimize all --print-source-code docs/book/examples/21-preservation.goat
```

```powershell
.\goat.exe --optimize none --print-source-code .\docs\book\examples\21-preservation.goat
.\goat.exe --optimize all --print-source-code .\docs\book\examples\21-preservation.goat
```

These commands also execute the program, so its output follows the reconstructed source. Use `--print-bytecode` to compare instructions. The representations may differ; the comparison should concern preserved behavior rather than identical listings.

[test_c_replacement.c](https://github.com/kniazkov/g0at/blob/b91b405e949cc93d4aa9d243be622c5b143cb858/src/test/test_c_replacement.c) checks constant use in C representation, while functional examples compare results with and without optimization. Error cases also require attention to `stderr` and exit status, rather than only successful output.

<a id="section-21-8"></a>

## 21.8. What reaches the native backend

By the end of part IV, the program has undergone several distinct checks. Abstract values described possible results; states described binding changes; the graph described call dependencies; effects described environment interactions; and C checks described permitted representations and bodies. Simplification used only facts authorizing changes to the relevant representation.

This sequence does not make the analyzer omniscient. It establishes explicit boundaries: what is proven, what remains incomplete, and what is unsupported. Part V starts where a selected specialization has already been admitted: constructing a C module that must preserve Goat's established rules.

### Removing unread bindings

After scalar and branch replacements, another pass counts reads in the executable tree. Archived originals do not keep a binding alive. References are matched by declaration identity, so shadowed names remain separate; reads from nested functions also count. A simple assignment to a mutable variable is a write, while an update such as `++` reads its old value. Assignments to constants remain because their failure is observable.

If a binding has no remaining reads, the pass removes its storage. An initializer that can safely be discarded disappears with it. Otherwise, the initializer remains as an expression statement, preserving calls, side effects, exceptions, and evaluation order. Simple stores to the removed variable become their right-hand expression; a discarded, safely removable assignment emits nothing. The pass repeats until no more bindings disappear. Removing an unused closure can therefore make its captures removable on the next pass.

Expression-deletion and statement-deletion nodes each have one child: the archived original. They emit no bytecode, including deferred function bodies. A deletion expression is used only where its value is discarded, so its surrounding statement emits no `POP`. Restoration unwraps both deletions and replacements before a new analysis.

For example, `a=2; b=3; x=a+b; println(x);` produces only `ILOAD32 5`, `VLOAD "println"`, `CALL 1`, `POP`, and `END`. The data-segment index of `println` may change because the removed names no longer occupy entries.

> [!CAUTION]
> Purity alone does not prove that evaluation terminates or avoids exceptions. Calls are retained unless an existing stronger transformation has already removed them. Updates that read a value and bindings in object-valued blocks are handled conservatively. Reconstructed declarations keep the executable form: a deleted declarator is omitted, a replaced declarator emits the replacement result, and a declaration whose declarators are all removed prints nothing. A mixed `for`-header initializer that keeps some declarators while replacing others retains its archived form, because a single header slot cannot express both; the graph and bytecode expose the storage deletion directly.

Native generation also honors deleted bindings. Before allocating C locals, each specialization computes the storage references in its own selected tree. Deleted declarations and stores are omitted when that tree does not need their binding; retained initializers still execute without allocating the unused local. If the signature-wide proof selects an archived expression that accesses a deleted binding, that binding and its initialization are restored. The check repeats because a restored initializer may require another binding. A VM-only constant observation therefore cannot remove storage needed by a generic native specialization.
