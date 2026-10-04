# 11. Memory Management

[Contents](index.md) · [Русский](../ru/11-memory-management.md) · [Previous chapter](10-contexts-and-closures.md) · [Next chapter](12-exceptions.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-11-1"></a>

## 11.1. Memory has several owners

“When should this object be freed?” has different answers at different stages. Tokens are generally needed until parsing finishes, an AST until compilation finishes, and a closure may outlive the call that created it. Goat therefore uses several mechanisms, each for a particular group of data.

| Data | Main mechanism |
|---|---|
| Tokens, AST, analysis data | Arenas freed as a whole |
| Runtime values | Reference counts and a reachability collector |
| Reusable object shells | Bounded process pools |
| Loaded native library | Separate reference-counted descriptors |
| All allocations through `ALLOC` | Size accounting; extra checks under `MEMORY_DEBUG` |

These mechanisms do not replace one another. Freeing the AST arena must not destroy an executing function's environment, for example; and an object's zero reference count need not immediately return its shell to the system allocator.

<a id="section-11-2"></a>

## 11.2. An arena frees a stage as a whole

An arena (large blocks serving many small allocations) is implemented in [arena.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/arena.c). A new record takes the next aligned portion of the current block; insufficient space causes another block to be allocated. Individual nodes are not freed separately. Destroying the arena frees its blocks.

This suits compilation: an AST contains many linked nodes that generally become unnecessary together. [launcher.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c) organizes source-position, token, graph, and error memory separately. Tokens can be removed after building the tree; data still needed by the graph must already have a suitable owner.

An arena simplifies lifetime without removing its boundaries. A pointer into a destroyed arena becomes invalid. Bytecode therefore copies the needed instructions and data into its own buffer, while native descriptors have separate ownership.

<a id="section-11-3"></a>

## 11.3. Reference counting handles the ordinary case

A dynamic object stores its number of owning references. Acquiring another such reference requires `INCREF`; releasing one requires `DECREF`. At zero, the object clears the references it retains and either frees its shell or puts it in a pool. Concrete type methods are in `src/model`.

Boundary contracts matter. The stack accepts a transferred reference; creating a property retains both key and value; a function retains its environment and parameter names. The caller can therefore decrement its own value reference after creating a property: the property already owns one.

Singletons—`null`, booleans, and other static objects—may use empty `INCREF` and `DECREF` methods. A macro appearing in caller code does not mean every object physically contains a mutable counter.

In [process.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/process.c), the process maintains an active dynamic-object list and separate pools for integers, reals, strings, and user-defined objects. Pools reuse already allocated shells. `ZOMBIE` denotes such a cleared shell, rather than a value the program can still access. For a user-defined object, `DYING` protects deep cleanup against reentry.

<a id="section-11-4"></a>

## 11.4. Why a counter is insufficient

If an environment holds a function and that function retains the environment, both references remain after the last external reference disappears. Their counts do not reach zero. This group is cyclic garbage: internally linked, but inaccessible to the program from outside.

[11-cycles.goat](../examples/11-cycles.goat) creates exactly this situation:

```goat
const make = func() {
    const self = func() { return self; };
    return self;
};
var saved = make();
println(saved() == saved);
saved = null;
println("released binding");
```

Result:

```text
true
released binding
```

`self` returns its own function object; equality confirms its identity. After `saved = null`, the external variable no longer retains the function, but the link between `self` and the environment remains.

The last line only reports program execution. It neither measures memory reclamation nor proves when collection occurs. Ownership checks require internal tests observing objects or resources, rather than just printed output.

<a id="section-11-5"></a>

## 11.5. Mark and sweep

[gc.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/gc.c) implements *mark-and-sweep* (marking reachable objects and removing the rest). Roots are the references where traversal starts. In the current code, they are process-cache strings and, for every thread, the current context's data, a pending exception, and all stack values.

An object's `mark` method marks it and continues through retained objects. A user-defined object visits keys, property values, and prototypes; a function visits its environment and parameter names. Checking for an existing mark stops repeated traversal around a cycle.

The collector then walks the process's active-object list. Unmarked objects are cleared; survivors have their marks reset for the next pass. This cleanup uses special paths without ordinary recursive reference decrements: the collector is already traversing the entire unreachable graph. Some shells go into pools.

A pending exception is deliberately a root. Its value may be a dynamic object no longer retained by destroyed contexts. [test_vm_exceptions.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_vm_exceptions.c) checks that such a value survives, including a repeated collector call.

> [!CAUTION]
> The ordinary VM loop invokes the collector after execution ends, rather than periodically by allocation count. Unreachable cycles can accumulate until then. Also, current marking starts at the current context's data and does not separately walk `context->previous`. This code cannot simply be moved to an arbitrary point inside a function call without further root checks.

<a id="section-11-6"></a>

## 11.6. A native library must stay alive too

A function object may contain a machine-code address from a loaded `.so` or `.dll`. Unloading the library before the function would invalidate that address. [native_library.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/model/native_library.c) therefore uses two levels: a function descriptor retains its library, while bytecode and dynamic function objects retain the descriptor.

These descriptor counters are atomic. This is a resource-management property; it does not by itself make the whole VM thread-safe. The final release unloads the library and cleans up associated resources. Destroying a dynamic function releases its descriptor both on ordinary `DECREF` and when collecting an unreachable cycle.

The lifetime test in [test_native_call.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_native_call.c) checks several routes: a retained function alias, a closure with a cycle, and process destruction. In particular, freeing bytecode must not unload a library whose native function is still retained by a live object.

This is not general permission to call a bytecode function after freeing its instructions: its body address still only makes sense with the corresponding bytecode. The native descriptor protects its own resource—the library.

<a id="section-11-7"></a>

## 11.7. Debug accounting and process destruction

The wrappers in [allocate.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/allocate.c) track allocated size and ensure alignment. Size overflow and allocation failure terminate the process with a diagnostic. Under `MEMORY_DEBUG`, they additionally record allocation file and line, maintain a block list, and place guard bytes after the payload. `FREE` checks these bytes; the remaining-block list helps investigate leaks.

[CMakeLists.txt](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/CMakeLists.txt) enables `MEMORY_DEBUG` for configurations other than `Release`. `Release` omits the extra records and guard-byte checks, but retains basic size accounting. Performance comparisons between builds must account for these differences.

> [!CAUTION]
> Guard bytes are not a complete memory-correctness check. This mechanism does not automatically detect every use-after-free read or every out-of-bounds write. It also accounts only for allocations made through the project's wrappers.

Process destruction frees threads, remaining active objects, pools, and the cache. It is the final ownership boundary, but it does not replace checking that temporary data is properly released during a long execution.
