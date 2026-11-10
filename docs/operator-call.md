# Callable user objects

[Русская версия](operator-call.ru.md)

A user object can provide an ordinary function-valued `"()"` property:

```goat
var counter = {};
counter.value = 4;
counter["()"] = func(step) { this.value += step; return this.value; };
println(counter(3)); // 7
```

The callee is evaluated and saved first, then arguments are evaluated once from
left to right. Only then is `"()"` looked up on the saved object and its prototypes.
Replacing that property in an argument affects this call. Reassigning the variable
holding the callee does not replace the saved object. A callee evaluation failure
prevents argument evaluation; a lookup or handler failure preserves earlier effects.

The handler receives the called object as `this`, including inherited handlers and
callable values obtained from named or indexed properties. For `holder.item(x)` or
`holder["item"](x)`, if item is a callable user object, `this` is item, not holder.
If item is an ordinary function, the existing method receiver remains holder.
Ordinary functions and methods are called directly, without consulting their
`"()"` properties. Built-in values do not gain user-defined call behavior.

The handler must itself be a function: a Goat function, closure, or built-in
function. Another callable user object is not accepted as a handler. Missing or
non-function properties raise `INVALID_OPERATION`. Access errors and failed-file
lookup errors propagate unchanged rather than becoming missing-handler errors.
Thrown values, source traces, and effects already performed are preserved.

Goat handlers receive all actual arguments in the read-only `arguments` array,
including extra arguments; missing formal parameters receive null. Zero arguments
are valid. Built-in handlers retain their own arity and type restrictions.

## Implementation and verification

Generation saves the callee and any method receiver on the operand stack before
arguments. Existing `ROTATE` instructions keep the arguments below those saved
values for `CALL` and `CALLM`. User-object calls reuse operator lookup and the
ordinary function continuation with the original callable receiver. The stack
and call frame retain live values across nested calls, GC, exceptions and finally.
Analysis follows the same evaluation order and keeps unknown call effects.

No opcode, bytecode-format version, migration path, or executable-image mutation
is introduced. Current compiled and serialized bytecode use the same contract.
Native operator-handler integration remains a separate task, #503; fallback VM
execution is not evidence that native handlers are integrated.

The C/CTest `operator-call` suite checks ordering, receivers, handlers, failure
provenance, lifetime, and optimized/unoptimized direct and serialized bytecode.
