# 10. Contexts, Functions, and Closures

[Contents](index.md) · [Русский](../ru/10-contexts-and-closures.md) · [Previous chapter](09-vm-and-values.md) · [Next chapter](11-memory-management.md)

Section 10.8 is updated for indexed access and `this` in every scope. Other sections describe: [commit ae26b84, method calls and the `this` constant](https://github.com/kniazkov/g0at/tree/ae26b849471233f3329214f3c170a7a26a756118).

<a id="section-10-1"></a>

## 10.1. Scope becomes data

In chapter 7, a scope connected a name to an AST declaration. Execution needs to store concrete values instead. This is the role of `context_t` in [context.h](https://github.com/kniazkov/g0at/blob/ae26b849471233f3329214f3c170a7a26a756118/src/model/context.h): a data object, a link to the previous context, and fields controlling returns or exceptions.

`create_context` creates a new user-defined object. Its prototype is either an explicitly supplied environment or the caller context's data. Declarations create own properties on this object. A nested block can therefore see outer names, while its own declaration can shadow one of them.

There are two distinct links: `previous` leads backward through execution, while data prototypes determine name lookup. For an ordinary block they agree. For a closure call they may lead to different environments. Confusing them would silently replace lexical scope with dynamic scope, where a name depended on the call site.

<a id="section-10-2"></a>

## 10.2. What a function retains

At `FUNC`, the VM creates a dynamic function object. It stores the first body instruction's address, formal parameter names, a reference to the current context's data, and an optional descriptor of native specializations. Reference counting retains the data reference. The implementation is in [function.c](https://github.com/kniazkov/g0at/blob/ae26b849471233f3329214f3c170a7a26a756118/src/model/function.c).

A closure is a function together with its saved lexical environment. Goat retains the environment object, rather than a snapshot of individual values or a pointer to a temporary C context structure. A write to an outer variable is therefore visible to later calls of the same function. Separate calls of a factory function create separate environments.

The bytecode body can still be shared. In the example below, both counter functions execute the same instructions but read different objects holding `value`.

The diagram shows these links while calling `a` in the example below. The execution context stores the return path, while the call's data object inherits the function's environment.

```mermaid
flowchart TD
    C["Context of the call to a"] -->|previous| P["Caller context"]
    C -->|data| D["Data of the call to a"]
    D -->|prototype| E["Data of the call to make(10)"]
    F["Function object a"] -->|closure| E
```

<a id="section-10-3"></a>

## 10.3. How a call proceeds

`CALL` takes the function object from the stack and invokes its `call` method. For a built-in, this directly calls its C executor. For a dynamic function, a suitable native specialization may be selected first; part V examines that path. Ordinary bytecode execution performs these steps:

1. Create a call context. Its `previous` points to the caller context, while its data prototype points to the function's saved environment.
2. Mark the context `FLOW_RETURN` and save the address of the instruction after `CALL`.
3. Pop actual arguments from the stack and bind them to formal parameters in order.
4. Bind missing arguments to `null`. Extra arguments have already been evaluated, but receive no parameter names and are released.
5. Reserve a result slot on the stack, initially containing `null`, and transfer execution to the first body instruction.

Argument evaluation proceeds left to right. Binding arguments to parameters proceeds left to right as well; the stack holds the evaluated values and releases them for binding. Names in the source are not reordered.

A method call such as `obj.method(...)` differs at the call site and in the call context. Code generation for `NODE_MEMBER_ACCESS` as the callee emits `VLOAD obj`, then `DUP`, `GETPROP`, and `CALLM` instead of `CALL`: the duplicate keeps the receiver under the function on the stack. `CALLM` pops the function and then the receiver below the arguments and passes both to the function's `call` method. Section 10.8 examines what the receiver means for the call context.

<a id="section-10-4"></a>

## 10.4. Returning through nested blocks

`RET` pops the return value and writes it into the reserved slot. The VM then destroys contexts up to the current call boundary, reduces the stack to the result, and restores the continuation address and caller context. A `return` inside several blocks therefore returns from the function, rather than just leaving the nearest block.

Ordinary block completion differs: `LEAVE` retains the block's data object, destroys the context wrapper, and pushes the object. `RESTORE` only removes the context. These instructions are not interchangeable: they have different stack effects.

Destroying a context decrements its data's reference count. It need not destroy that data immediately. If a function retains the environment, the values remain available after the call that created them returns.

A `for` loop owns one context for header bindings and creates a nested body context per iteration. Retaining a closure over a body-local variable retains that iteration's object; retaining the header counter observes its later updates. A body written without braces receives the same nested scope. Return and exception handling unwind these contexts through their existing runtime paths.

<a id="section-10-5"></a>

## 10.5. Independent states and recursion

File [10-closures.goat](../examples/10-closures.goat):

```goat
const make = func(start) {
    var value = start;
    return func() { value = value + 1; return value; };
};
const a = make(10);
const b = make(20);
println(a());
println(a());
println(b());
const first = func(x) { return x; };
println(first());
println(first(7, 8));
const factorial = func(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
};
println(factorial(5));
```

Result:

```text
11
12
21
null
7
120
```

`a` and `b` retain data from different calls to `make`. Two calls to `a` modify one variable, while `b` starts from its own value. `first` demonstrates that a user function does not check for an exact argument count: a missing argument becomes `null`, and an extra one is unused.

The recursive `factorial` finds its own name through the environment. Each call creates a new context with its own `n` and return address. An environment may retain a function that retains that same environment, creating a reference cycle. Chapter 11 examines its memory consequences.

> [!CAUTION]
> Bytecode calls do not perform tail-call optimization: each such call creates another context. The current executor has no general user mechanism limiting VM recursion depth; context and stack growth are bounded by available memory. Native-path protective limits are a separate mechanism.

<a id="section-10-6"></a>

## 10.6. Processes and threads in this implementation

`process_t` owns shared runtime state and a circular list of `thread_t` records.
Each thread retains its current context, data stack, instruction index, operands,
exception and native-attempt counters. Since #217, `spawn(callable, ...arguments)`
starts an OS-backed execution in this same model; `join(id)` returns its result or
propagates its original exception. A recursive execution mutex protects shared
model operations and is released between VM instructions and around blocking waits.

All program threads are equal. Finishing the entry execution does not release
shared state or terminate other threads; ordinary cleanup waits for the last
program execution, including descendants created by surviving threads. A distinct
recursive initialization mutex serializes include/import, with dynamic spawn/join
restrictions. The directly executed entry file is excluded from those restrictions.
See [concurrency API](../../module-serialization-api.md).

A Goat `process_t` is a runtime structure, not an automatically created operating-
system process. Internal OS mutexes do not introduce user-language mutex objects.

<a id="section-10-7"></a>

## 10.7. What outlives a call

After a return, the context wrapper and temporary call values are no longer needed. However, the returned object, captured environment, and data reachable through them may live on. This is not an exceptional case; it is the basis of ordinary closure behavior.

It helps to ask two separate questions: where `RET` returns control, and who retains the data afterward. Contexts and addresses determine the first; object references determine the second. The next chapter describes that second side, including cases where reference counts cannot release an environment on their own.

<a id="section-10-8"></a>

## 10.8. Method calls: the receiver and `this`

A method call passes the receiver, the object on which the method is invoked, into the call. `CALLM` hands it to `dynamic_call`, which builds the call's data object from a list of prototypes: for a method call the list contains the function's closure followed by the receiver; an ordinary call keeps only the closure. Name lookup through `VLOAD` therefore checks own properties, then the closure's environment, then the receiver's properties. An update of an undeclared name inside a method body (Section 7.6) is resolved by the same walk: `STORE` finds an existing property through the chain or creates one in the call's own data.

**In Goat, `this` is the object of the current scope. This differs fundamentally from the familiar Java and JavaScript semantics.** In a Java instance method, `this` denotes the current class instance. In JavaScript, an ordinary property call `obj.method()` supplies `obj` as `this` to an unbound ordinary function; arrow functions have lexical `this`. In Goat, even inside `obj.method()`, `this` denotes a separate call-context object, **not `obj`**. The receiver is accessible through that context's prototypes: `this.x` can find its property, but `this == obj` is false.

No function call is required: `this` works at the top level, in an object block `{ ... }`, in nested blocks and in ordinary functions. Every new context has its own `this`; leaving a block restores the previous current context. Variables, parameters and constants cannot be named `this`; assigning the `"this"` property raises `PROPERTY_IS_CONSTANT`.

```goat
var obj = {
    this["x"] = 1;
    this[true] = 2;
    var inner = {
        var x = 3;
        this[false] = 4;
    };
};
println(obj.x);             // 1
println(obj[true]);         // 2
println(obj.inner[false]);  // 4
println(obj.inner.this == obj.inner); // true

obj.inspect = func() {
    println(this == obj);   // false: a separate call context
    println(this.x);        // 1: found through a prototype
    this["local"] = 9;      // a new property of the call context
    return this;
};
var context = obj.inspect();
println(context.local);     // 9
println(obj.local);         // null
```

**Important:** writes through `this` follow ordinary Goat property rules. An existing property may be updated on a prototype; a new property is created on the current object. Use `var x` or `const x` when a nested block needs its own `x` shadowing an outer property. The implicit declaration `x = 1` creates a constant; `this["x"] = 1` creates an ordinary mutable property when it does not exist yet.

User-defined objects expose `this` as an implicit constant property without inserting a dictionary entry: an empty object stays empty and false. Call contexts retain their previously explicit `this` property for compatibility. Both forms refer to the object itself without requiring a strong reference cycle. See the [language reference](appendix-b-language.md#section-b-index) for indexed access.

```goat
const obj = {};
obj.x = 0;
obj.getX = func() { return ++x; };
println(obj.getX());
println(obj.getX());
```

The method's body never names `obj`. Both calls still find `x` through the receiver's prototype and print `1`, then `2`. A function obtained from a factory and installed on an object behaves the same way: the receiver is determined by the object on which the method is called, not by where the function was created.
