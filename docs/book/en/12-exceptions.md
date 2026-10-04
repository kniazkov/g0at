# 12. Exceptions and Runtime Errors

[Contents](index.md) · [Русский](../ru/12-exceptions.md) · [Previous chapter](11-memory-management.md) · [Next chapter](13-builtins.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-12-1"></a>

## 12.1. An exception is a value taking another route

An ordinary expression passes its result to the next operation. `throw` passes a value to a handler, skipping the unfinished computation. In Goat, any value can be an exception, including a string, number, object, or `null`. No particular exception class is required.

Standard operation errors use strings such as `DIVISION_BY_ZERO`, `INVALID_ARGUMENT`, and `INVALID_OPERATION`. [exceptions.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/exceptions.c) provides an immutable `Exceptions` object with standard names. These strings remain ordinary values: returning an error text normally does not itself raise an exception.

The language value `null` also differs from a null C pointer. It has an object. Thus, `throw null` means a real exception whose value is `null`, rather than an absence of error.

<a id="section-12-2"></a>

## 12.2. An operation result carries a flag

In [object.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/object.h), `operation_result_t` contains a value pointer and `is_exception`. `operation_success` and `operation_exception` construct its two variants. The contract requires a non-null pointer to a value owned by the result.

An arithmetic VM handler pops its operands, calls an object method, and releases the consumed references. It then checks the result flag: a normal value goes on the stack; an exceptional one goes to `dispatch_exception`. One instruction can therefore handle both a successful computation and division by zero.

Built-ins return the same result type. Their common wrapper releases arguments and, on error, stores the value in `thread->exception` and reports failure. The `CALL` handler takes that value and starts the common handler search. Built-in errors therefore use the same mechanism as an explicit `throw`.

<a id="section-12-3"></a>

## 12.3. How try/catch is compiled

[try_catch.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/try_catch.c) emits `TRY` with a handler address. At runtime, this instruction creates a context marked `FLOW_THROW`, saving the address and the entry stack boundary.

After the protected body completes normally, `RESTORE` removes the handler context and `JUMP` skips `catch`. On the exceptional path, the VM itself transfers control to the handler and pushes the exception. Its beginning is `ENTER; VAR`: a new context receives the local exception name. The `catch` ends with `RESTORE`.

This is a control-flow scheme, rather than a promise to execute both blocks. If the protected body finishes normally, the `catch` body does not run. If the body returns from the function, `RET` unwinds the required contexts along its own route.

> [!CAUTION]
> This version implements `try/catch`, but has no `finally` and no automatic invocation of user resource-cleanup handlers on scope exit. Such guarantees from other languages must not be assumed for this syntax.

<a id="section-12-4"></a>

## 12.4. Finding a handler and cleaning up

`THROW` pops a value and transfers its ownership to `dispatch_exception` in [vm.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/vm.c). That function follows `context->previous` to the nearest `FLOW_THROW`. This searches the execution chain: an error in a callee can reach the caller's `catch`.

Nested contexts are then destroyed, and the stack is reduced to the saved boundary. Temporary values above it receive `DECREF`. The auxiliary `ARG` operand buffer is cleared. The exception value itself is retained separately and must not disappear with the context where it arose.

When a handler is found, its service context is also removed, the exception is pushed, and the instruction index changes to the `catch` address. A new `throw` inside `catch` therefore searches for an outer handler: the old `TRY` does not remain active for its own handler.

If no handler exists, unwinding reaches the root context, the temporary stack is cleared, and the value is saved in the thread. The VM stops with a nonzero status. [binary_program.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/binary_program.c) prints an uncaught-exception diagnostic. While that diagnostic needs it, the value remains a garbage-collector root.

<a id="section-12-5"></a>

## 12.5. An unfinished expression does not resume

File [12-exceptions.goat](../examples/12-exceptions.goat):

```goat
const divide = func(x) { return 10 / x; };
try {
    println(100 + divide(0));
} catch (error) {
    println(error);
}
try {
    try { throw "stop"; } catch (inner) { throw inner; }
} catch (outer) {
    println(outer);
}
println(2 + 3);
```

Result:

```text
DIVISION_BY_ZERO
stop
5
```

Before `divide(0)` is called, `100` is already on the stack as the left operand of addition. The error does not return an arbitrary number in place of the quotient. It cancels the unfinished addition and outer `println`, clears temporary values, and enters `catch`. No line containing the result of `100 + ...` is therefore printed.

The second fragment passes a string through two handlers. `inner` exists only in its handler; the outer `catch` receives the same thrown value as `outer`. The final line shows ordinary computation continuing after handling.

<a id="section-12-6"></a>

## 12.6. Four different failure categories

| Category | Example | Outcome |
|---|---|---|
| Language exception | Division by zero, `throw`, invalid built-in argument | Search for `catch`; terminate with a diagnostic if absent |
| Compilation error | An invalid syntactic construct | Diagnostic before program execution |
| Native-path failure | Library preparation fails or a native call returns a failure status | Mode and status determine VM execution, a restricted retry, or termination |
| Internal invariant violation | Insufficient stack values, corrupted guard bytes | Abrupt termination through implementation checks |

A source-level `catch` cannot handle an error in parsing its own program: execution has not started. Nor does it turn every loader or C allocator failure into a Goat value.

Retrying in the VM after a native call's protective limit has specific conditions: a pure specialization (one that does not change observable external state), retained arguments, and an eligible status. It is not a universal rule to “try again after any error.” Part V covers the details.

<a id="section-12-7"></a>

## 12.7. What the tests check

[test_vm_exceptions.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_vm_exceptions.c) checks handler transfer, call unwinding, returns through protected regions, temporary-value cleanup, and retention of an uncaught object. It separately checks that `null` can be an exception value. These test execution structure, rather than just matching output strings.

[test_operation_result.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_operation_result.c) checks the contracts for normal and exceptional operation results. Both levels matter because a correct error value does not guarantee a correct stack afterward. If the next statement after `catch` receives leftovers from another expression, handling remains broken even when the first diagnostic line is correct.

> [!CAUTION]
> Exception handling does not isolate arbitrary machine code and is not a sandbox. Internal crashes and memory errors in the C implementation have no general guarantee of safe interception at Goat level.
