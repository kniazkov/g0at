# 19. Effects and Purity

[Contents](index.md) · [Русский](../ru/19-effects-and-purity.md) · [Previous chapter](18-call-graph-and-recursion.md) · [Next chapter](20-native-eligibility.md)

Revision 6. Implementation described: [commit fc4af9d](https://github.com/kniazkov/g0at/tree/fc4af9d85d2a45925e9e370a1e988ccb077b056e).

<a id="section-19-1"></a>

## 19.1. A result is not the whole behavior

Two functions may return the same number, while one also prints a line or changes an external variable. Replacing their calls with the same constant would change behavior. Analysis therefore stores effects separately from return types.

A pure function in the sense used here neither depends on mutable external state nor changes observable state outside itself. Local calculations and changes to its own locals do not prevent purity. Reading an external mutable variable does: identical arguments no longer determine behavior independently of the environment.

Purity does not mean that a function must terminate or never throw. Those properties require other proofs. In particular, an arbitrary pure call cannot be deleted merely because its normal return type is known.

<a id="section-19-2"></a>

## 19.2. Direct effects and captures

[function_effects.c](https://github.com/kniazkov/g0at/blob/fc4af9d85d2a45925e9e370a1e988ccb077b056e/src/analysis/function_effects.c) walks body syntax and invokes nodes' `collect_direct_effects` methods. A nested function body is analyzed for its own summary, rather than treated as executed when the outer body creates the function.

For a variable access, analysis finds the nearest function owning its declaration. Access is local if that is the current function. Otherwise, it records a capture with read and write flags. A capture contains both declarator and name: the name distinguishes, among other things, built-in bindings sharing a synthetic declarator.

Assigning local `y` does not become an external effect merely because `=` appears. Writing captured `total`, in contrast, receives `external-write`. If the assignment also reads the old value, it adds `external-read`.

Calls are recorded separately by source site. At this stage, the function's own syntax does not yet reveal the callee body's full effect.

<a id="section-19-3"></a>

## 19.3. What information is recorded

| Flag | Meaning |
|---|---|
| `none` | No possible external effects in this summary |
| `input` | Input |
| `output` | Output |
| `external-read` | Reading an external binding |
| `external-write` | Writing an external binding |
| `unknown` | The complete effect set is not established |

These are bit flags: several can coexist. The general effect vocabulary must be distinguished from achieved precision. Built-in descriptors directly describe input and output, but the current interprocedural pass does not always transfer that precise description into a user-function summary.

> [!CAUTION]
> Effect propagation currently uses user specializations as precise targets. A built-in or another unresolved call may produce `unknown`, even when a reader knows the function only prints or computes a square root. This is missing proof, rather than a claim that the function actually performs arbitrary actions.

<a id="section-19-4"></a>

## 19.4. Immutable reads and propagation

[function_purity.c](https://github.com/kniazkov/g0at/blob/fc4af9d85d2a45925e9e370a1e988ccb077b056e/src/analysis/function_purity.c) starts from direct effects. If every recorded external read refers to a `const` declaration, it removes the external-read bit from the resulting own effects. The capture list remains: the later native contract still needs it.

A root-provided built-in binding does not count as such a proven local `const`. Unknown effects and external writes do not disappear merely because some reads are immutable.

Callee user-specialization effects then join caller effects. The pass repeats until nothing changes. A write in a third function thus becomes a possible effect of the first, even if the first only calls the second.

Every other syntactic call needs coverage by graph edges. A missing edge does not prove purity: `unknown` is added. This matters for calls in branches dependency discovery may have skipped.

> [!CAUTION]
> Direct effect collection is syntactic: it does not automatically exclude every unreachable action. Propagation is limited to 64 passes. A truncated graph or exhausted limit adds `unknown` to all its summaries so an unfinished result cannot appear to prove purity.

A dedicated proof recognizes the original built-in `abs`, including immutable aliases such as `const magnitude = abs`. It follows declarations rather than names and checks the program for writes to the built-in binding. A user function named `abs` remains a user function. With this proof, reading the binding and calling the numeric operation add no external effects. Such calls do not require a user-specialization graph edge.

<a id="section-19-5"></a>

## 19.5. Three small functions

File [19-effects.goat](../examples/19-effects.goat):

```goat
const offset = 2;
var total = 0;
const local = func(x) { var y = x; y = y + 1; return y; };
const captured = func(x) { return x + offset; };
const changed = func(x) { total = total + x; return total; };
println(local(3));
println(captured(3));
println(changed(3));
```

Result:

```text
4
5
3
```

For its integer signature, `local` has `effects=none`: the write only changes its own local. `captured` retains the direct read of `offset` in its capture list, but its final effects are also `none`, because the binding is immutable. Its C eligibility is nevertheless `unsupported`: purity alone does not permit transferring arbitrary captured values into native code.

`changed` retains `external-read|external-write`, while its purity field prints `unknown`. This means “purity not proven,” rather than providing a separate exact classifier for every kind of impure function.

<a id="section-19-6"></a>

## 19.6. Possible effects and performed effects

A summary describes *may-effects*: effects analysis must account for as possible. It does not assert that every call necessarily performs every recorded write or print. A conditional around an action makes this particularly apparent.

The opposite mistake is treating missing observation as proof of absence. If the graph cannot resolve a call target, a function cannot be declared pure merely because its visible body does not change anything.

`function_summary_is_pure` checks that final effects are absent. The function node's overall flag is stricter in another sense: it intersects properties across all registered signatures. An empty specialization set proves nothing. A graph node's color therefore does not replace reading the labeled summary of the required signature.

<a id="section-19-7"></a>

## 19.7. Why this boundary matters

Purity is used in native-subset eligibility and in restricted VM retries after a native-path protective failure. Retrying a function with an external write could perform an action twice. A numeric result is therefore insufficient for that decision.

[test_function_effects.c](https://github.com/kniazkov/g0at/blob/fc4af9d85d2a45925e9e370a1e988ccb077b056e/src/test/test_function_effects.c) checks own accesses and captures; [test_function_purity.c](https://github.com/kniazkov/g0at/blob/fc4af9d85d2a45925e9e370a1e988ccb077b056e/src/test/test_function_purity.c) checks propagation through dependencies. These checks are separate from return types: changing numeric-domain precision must not by itself turn an external write into a local one.

The next chapter combines these facts with parameter, local, and return representations. Only then can a particular specialization be admitted to C generation.
