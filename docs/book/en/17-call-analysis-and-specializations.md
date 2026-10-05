# 17. Call Analysis and Specializations

[Contents](index.md) · [Русский](../ru/17-call-analysis-and-specializations.md) · [Previous chapter](16-expression-and-control-analysis.md) · [Next chapter](18-call-graph-and-recursion.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-17-1"></a>

## 17.1. A call requires knowing its target

Addition analysis needs two operand descriptions. Call analysis also needs information about the function and its environment. `LATTICE_KNOWN_FUNCTION` carries a user body and lexical activation identity, or a built-in descriptor. General `FUNCTION` says only that a value is callable, not which body will run.

Two functions created from one `func` node in different factory calls may capture different data. An AST pointer alone is therefore insufficient for precise behavior analysis. Activation identity keeps ordinary analysis from confusing those environments.

For a built-in, the known descriptor selects the `interpret` method from chapter 13. Name spelling alone cannot replace this knowledge. A user function may be traversed within limits using the caller's current facts.

<a id="section-17-2"></a>

## 17.2. Analyzing one known call

[function_call.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/function_call.c) first checks that the path and arguments have normal values. For a user function, it then registers a type specialization, creates an abstract activation, and clones caller state. Local values from a previous traversal of that function are reset.

Parameters receive actual-argument descriptions. Missing parameters receive `null`; extras create no parameters, but have already been evaluated and can end the path. The body is traversed until normal execution ends. Each `return` contributes a value and saves a normal-exit state. Reaching the end contributes `null`.

Exit states are joined. Captured-variable changes return to the caller, while the function's current local values do not replace the caller's own entries. Declaration observations are retained even from terminated paths.

This is context-sensitive analysis: an argument of `2` may reveal more than an arbitrary integer. But its result concerns this investigated call, rather than automatically every future call.

<a id="section-17-3"></a>

## 17.3. Where traversal stops

> [!CAUTION]
> Ordinary call analysis is limited to depth 32 and a shared budget of 1024 precise user-body entries. It also stops precise traversal when reentering an already active body, when the captured environment's owner is inactive, or when the body contains `try/catch`. In these cases, facts are forgotten and the result expands to `TOP`. These are analyzer limits, rather than limits on the program's allowed call count.

Environment ownership particularly matters for a returned closure. The VM can retain its data after the factory exits, but ordinary abstract analysis does not reconstruct arbitrary completed environments with the same precision. “Works in the VM” and “fully known to the analyzer” differ again.

An unknown call must not receive an invented result either. Possible binding changes require discarding some current facts. Recursion is not prohibited: it has the separate type-analysis mechanism of the next chapter.

<a id="section-17-4"></a>

## 17.4. How a specialization is formed

`function_summary_set_t` stores records keyed by an ordered tuple of formal parameter types. Registration in [function_summary.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/function_summary.c) removes refinements: integer constants and ranges become `integer`, real constants become `real`, known functions become `function`, and typed arrays become `array`.

Calls with `2` and `3` therefore register one `(integer)` signature, while a call with `2.5` registers `(real)`. Order matters: `(integer, real)` differs from `(real, integer)`. Missing parameters enter the key as `null`; extras do not enter it. However, `BOTTOM` even in an extra evaluated argument prevents registering a completed call.

A function with no registered call receives no invented “just in case” specialization. Records arise from observed calls and may subsequently be added while exploring the dependency graph.

<a id="section-17-5"></a>

## 17.5. Reanalysis for every value of a type

A summary contains a return type, effects, captures, analysis status, and an independent C-eligibility status. To obtain a signature's return type, [function_return.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/analysis/function_return.c) investigates the body again: parameters now have the key's general types, while external bindings receive no incidental values from one call.

This analysis must account for branches not selected by the original concrete argument. Returning a number for `x = 1` does not exclude a string for `x = 0`, even though both inputs have type `integer`.

Statuses `unanalyzed`, `analyzing`, `analyzed`, and `inconclusive` describe the attempt's progress. `analyzed` does not mean everything is known: a completed analysis may produce `TOP`. `inconclusive` means the necessary analysis has not completed sufficiently for the corresponding conclusion. Native eligibility is checked separately.

<a id="section-17-6"></a>

## 17.6. An example with a write and two results

File [17-calls.goat](../examples/17-calls.goat):

```goat
var total = 0;
const add = func(x) { total = total + x; return total; };
println(add(2));
println(add(3));
const choose = func(x) {
    if (x) { return 1; }
    return "zero";
};
println(choose(1));
println(choose(0));
```

Result:

```text
2
5
1
zero
```

Ordinary traversal of the first two calls sees `total` change to `2`, then `5`. But the type summary for `add(integer)` cannot bind its result to those two numbers: the key does not fix `total`'s external state.

Both `choose` calls share the `(integer)` signature. Its normal results include an integer and a string, so the type summary contains `not null`. This is broader than either call's exact result, but covers the considered parameter values of that type.

<a id="section-17-7"></a>

## 17.7. A summary is not an execution-result cache

A summary stores proofs and approximations, rather than a result that can replace executing a function. Its key has no concrete arguments or capture values. Even a pure function with a known return type must compute the value at runtime.

Analysis events retain snapshots of mutable summary records; AST pointers and immutable elements remain borrowed. Diagnostic history must therefore not change retroactively when the current summary is refined again.

[test_function_specialization.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/test/test_function_specialization.c) and [test_function_summary.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/test/test_function_summary.c) check registration and the separation of these meanings. The next task is connecting specializations through calls, including cycles, to obtain information a single body traversal cannot provide.
