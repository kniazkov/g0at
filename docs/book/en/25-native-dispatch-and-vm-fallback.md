# 25. Native Dispatch and VM Fallback

[Contents](index.md) · [Русский](../ru/25-native-dispatch-and-vm-fallback.md) · [Previous chapter](24-library-compilation-and-loading.md) · [Next chapter](26-separate-compilation.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-25-1"></a>

## 25.1. One call, two execution paths

A user writes an ordinary call such as `calculate(3)`. Selecting native code requires no different syntax and does not change the variable holding the function. Dispatch (choosing the concrete execution path for a call) occurs inside the familiar `CALL` instruction.

Preparation binds a native-specialization descriptor to a `FUNC` instruction. When the VM creates the function object, it receives a reference to that descriptor alongside its ordinary data: parameters, bytecode address, and closure. The body bytecode remains available even when a native version exists.

In [function.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/function.c), selection precedes allocation of the bytecode call context. This is where analysis results, a loaded library, and actual runtime arguments meet.

<a id="section-25-2"></a>

## 25.2. Selecting by actual types

By `CALL`, argument expressions have been evaluated and their objects are on the VM stack. Dispatch inspects them without popping: there must be enough formal arguments, and each object in use must exactly match the selected specialization's `I64` or `F64` tag.

If no entry matches, the VM executes the body. An integer argument is not converted to a real merely to fit a native signature. Nor is a missing argument filled in for a native call: the ordinary VM will bind it to `null`.

Extra arguments do not prevent selection by formal parameters. Their expressions have already executed; only the unneeded value is discarded. File [25-dispatch.goat](../examples/25-dispatch.goat):

```goat
const identity = func(x) { return x; };
println(identity(3));
println(identity(1.5));
println(identity("text"));
println(identity());
println(identity(7, println("extra")));
```

Result:

```text
3
1.5
text
null
extra
7
```

After successful native preparation, numeric calls use two specializations of `identity`. The string call and the call without an argument remain in the VM. `extra` is printed once before entering `identity`; purity of its body does not make the entire call expression, including its arguments, pure.

<a id="section-25-3"></a>

## 25.3. Success is committed after result validation

Before invoking the adapter, required numbers are copied from objects into an ABI-value array. Original objects remain on the stack. `attempts` increases immediately before entering the adapter.

On `OK`, the expected result type and zero reserved field are checked. The number is then boxed into an ordinary Goat object, all actual arguments are popped with reference releases, the result is pushed, and the instruction pointer advances beyond `CALL`. `succeeded` increases.

Until that point, an unsuccessful attempt does not consume arguments. This matters for a permitted retry: the VM can begin the body with the same values without evaluating argument expressions again.

<a id="section-25-4"></a>

## 25.4. Which resources are bounded

An ordinary C call uses the process's machine stack. Native recursion therefore cannot replace a sequence of VM contexts without limits. Protection operates at two levels.

Before adapter entry, a platform check requires at least 512 KiB of stack headroom. Linux obtains thread-stack information through `pthread_getattr_np`; Windows uses `VirtualQuery`. If headroom cannot be confirmed, the call goes to the VM without a native attempt.

A separate guard operates inside the generated module:

| Limit | Current value |
|---|---:|
| Simultaneous entries into generated functions | 32 |
| Shared function-entry and loop-iteration budget per adapter call | 4096 |
| Distance from the adapter's stack marker | 65,536 bytes |
| Parameters + locals + temporaries in one definition | At most 128 |

Sequential calls and entered loop iterations spend the shared budget, whereas depth decreases on function return. A loop iteration spends one unit without changing depth. A broad call tree can therefore exhaust the budget without great depth. The limit of 128 is checked during generation; the others are checked during execution.

The adapter installs local guard state through a thread-local pointer and `setjmp` (a destination for a nonlocal return in C). An entry check that exceeds a limit uses `longjmp`; the adapter then restores previous state and returns `RESOURCE_LIMIT`. Internal functions are marked `noinline` so external optimization does not remove the intended frame boundaries.

> [!CAUTION]
> These are fixed limits of the current backend, not language parameters. The guard does not catch arbitrary memory corruption or hardware stack overflow, and it does not limit total VM runtime. Falling back may continue a long-running or nonterminating computation.

<a id="section-25-5"></a>

## 25.5. When the body may start again

A retry is permitted when sufficient stack headroom cannot be confirmed before entry; after entry, it is permitted for `RESOURCE_LIMIT` from an entry marked `PURE`. The latter check matters because part of the body may already have executed. A pure function leaves no external changes that repetition would duplicate.

Retry starts at the beginning of the bytecode body, not at the interrupted C call. Already evaluated arguments are reused. The new context and its descendants carry `native_disabled`, preventing recursive continuation from reentering the same native chain. `retries` increases.

Other adapter failures, including ABI mismatch, an invalid result, or external failure, do not silently cause a second execution of the body. Execution ends with a native-backend error. This differs from having no matching specialization, where a native attempt never began.

File [25-fallback.goat](../examples/25-fallback.goat):

```goat
const count = func(n) {
    if (n <= 0) { return 0; }
    return count(n - 1) + 1;
};
println(count(100));
```

With ordinary sufficient stack headroom, this command:

```sh
./goat --native required --print-native docs/book/examples/25-fallback.goat
```

produces:

```text
100

mode=required
preparation=ready
bound=1
omitted=0
attempts=1
succeeded=0
retries=1
```

Native call depth reaches its limit, after which the VM computes `100`. Internal recursive C calls are not separate `attempts`: the report counts crossings of the VM/adapter boundary.

<a id="section-25-6"></a>

## 25.6. What off, auto, and required mean

| Mode | Preparation | No matching signature at a call | Permitted resource failure |
|---|---|---|---|
| `off` | Native preparation is skipped | VM | No native attempt |
| `auto` | Failure permits continuing in the VM | VM | VM retry |
| `required` | At least one bound function is required | VM | VM retry |

`required` requires successful preparation, not native execution of every call. The recursive example therefore completes correctly in this mode too. Preparation without available functions is `empty`; compilation failure is `compile-error`, loading failure is `load-error`, and binding failure is `bind-error`. There is also `io-error`. In `auto`, an empty inventory is acceptable, while preparation failures produce diagnostics.

The default for an ordinary source file is `off`. For `--run` of a saved `.gbin`, it is `auto`. Native preparation is incompatible with `--optimize none` because it needs analysis proofs.

<a id="section-25-7"></a>

## 25.7. Reading the report

`bound` counts bound functions, not specializations; `omitted` counts rejected generator candidates. `attempts`, `succeeded`, and `retries` describe execution and are summed across VM threads. A failed preliminary stack check can produce a retry without increasing `attempts`.

For `25-dispatch.goat` with normal stack headroom, the values are `bound=1`, `attempts=3`, `succeeded=3`, and `retries=0`. The other two `identity` calls execute in bytecode and are not failed native attempts. With `--compile`, there is no execution, so call counters remain zero even after successful preparation.

CLI policy and reporting are in [native_execution.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/cli/native_execution.c); the adapter guard is in [c_adapter.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/codegen/c_adapter.c). [check_native_call.sh](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_native_call.sh) checks the call boundary, while [check_native_modes.sh](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_native_modes.sh) checks modes, effects, and VM fallback.
