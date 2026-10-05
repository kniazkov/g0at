# 9. Virtual Machine and Value Model

[Contents](index.md) · [Русский](../ru/09-vm-and-values.md) · [Previous chapter](08-bytecode-generation.md) · [Next chapter](10-contexts-and-closures.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-9-1"></a>

## 9.1. An executor that reads instructions

After generation, the VM no longer has the source tree. It receives a `bytecode_t` and a process holding execution state. The main loop in [vm.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/vm.c) takes the instruction index from `thread->instr_id`, checks bounds, selects a handler by `opcode`, and calls it. The handler table matches the opcode enumeration.

An ordinary handler advances the instruction index. A jump writes another index; calls and returns change it along with the context. The loop therefore does not perform an unconditional `instr_id++`: that would skip the first instruction after a jump. `END` stops execution. An invalid address or unknown opcode produces a diagnostic and a nonzero status.

This loop performs dispatch (selecting an action by its operation code). Goat implements it in C with a table of function pointers. Having bytecode does not mean that the processor recognizes `VLOAD` or `RET`.

<a id="section-9-2"></a>

## 9.2. The stack holds object pointers

[object_stack.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/object_stack.c) implements a growing array of `object_t *`. Its initial capacity is 128 elements, doubling when full. The value `3` is not stored in a stack cell as an embedded integer: the cell refers to an object representing the number.

`push` and `pop` do not themselves increment or decrement reference counts. They transfer ownership of a reference between their caller and the stack. The VM's `POP` instruction does remove a value and call `DECREF`. `DUP` creates a second owning reference and first calls `INCREF`. This distinction explains why mechanically inserting another `push` can break memory management.

Operations check that the stack contains enough values. Breaking this internal contract terminates the process through `fail_stack_underflow`; it is not an exception a user can handle with `catch`. Chapter 12 separates these error categories.

<a id="section-9-3"></a>

## 9.3. A shared header, different contents

[object.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/object.h) defines the common object structure and `object_vtbl_t`, a method table (addresses of C functions implementing value operations). Concrete types add fields and select their methods.

| Value | Representation and behavior |
|---|---|
| Integer | Signed 64-bit value; static and dynamic object implementations |
| Real number | A `double` value |
| String | Wide characters and length; static and dynamic objects |
| Boolean | Shared `true` and `false` objects |
| `null` | A separate shared object for an absent value |
| User-defined object | Properties, prototypes, and a computed ancestor order |
| Function | A built-in descriptor, or a bytecode body, parameters, and environment |

A shared object reused throughout the program is called a singleton. Its memory management methods may do nothing. A dynamic object generally belongs to a process and participates in reference counting and garbage collection.

The method table's `type` field contains broad categories. Integers and reals both belong to `TYPE_NUMBER`; `TYPE_OTHER` covers several other kinds. This field is therefore not a complete enumeration of language types. Separate checks such as `is_integer_object` select the exact numeric representation.

<a id="section-9-4"></a>

## 9.4. One instruction, different operations

A binary-operation handler pops two objects, passes them to the appropriate method, and releases the consumed references. The method returns an `operation_result_t`: a value and an exception flag. A normal value goes on the stack; an exceptional one goes to error handling.

Thus, `ADD` need not itself know every form of addition. Numeric methods implement arithmetic, while the string method joins a string with the right operand's text representation. Model methods return an appropriate error result for unsupported operations.

The language rules remain those from chapter 2: integer arithmetic uses its specified overflow behavior, and dividing two integers may return a real when the quotient is fractional. This follows the principle of least surprise: `3 / 2` preserves its fractional part. Mixed comparisons have special precision checks; describing them as unconditional conversion of both numbers to `double` would be incorrect. Implementations are in [integer.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/integer.c), [real.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/real.c), and [string.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/string.c).

<a id="section-9-5"></a>

## 9.5. Truthiness is a method too

For a branch, the VM asks for an object's boolean value. `null`, `false`, numeric zero, an empty string, and an empty user-defined object are false. Functions are true. A real `NaN` is also true here: the nonzero test succeeds.

`BOOL` and `LNOT` obtain this property and produce a boolean result. `LAND` and `LOR` additionally control short-circuit jumps. Consequently, Goat's `&&` and `||` produce booleans; they must not automatically be read as selecting one of the original objects.

[09-values.goat](../examples/09-values.goat) collects several observations about this model:

```goat
println(6 / 3);
println(3 / 2);
println("value=" + 3);
println(!"");
println(!{});
println({ var x = 2; const y = 3; });
```

Result:

```text
2
1.5
value=3
true
true
{"x":2,"y":3}
```

The braces in the final source line form a block expression. `ENTER` creates a context, declarations fill its data, and `LEAVE` returns the block's data object. This explains the result without assuming a separate object-literal syntax.

<a id="section-9-6"></a>

## 9.6. Properties and prototypes

A prototype is an object in which inherited properties can be sought. A user-defined object stores its own properties in an AVL tree, its keys, and references to prototypes. It precomputes a topology (an ordered list of objects to search next) for ancestor traversal. Building it accounts for shared ancestors. The code is in [user_defined_object.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/model/user_defined_object.c).

When loading a variable, the VM first requests an own property of the current context's data object, then checks ancestors in topology order. If no property is found, the result is `null`. When writing, `STORE` looks for an existing writable location; an attempt to modify a constant raises an exception. If no suitable property exists, it creates a local one.

Separate table methods read, create, and modify properties. A name's existence, the ability to read it, and the ability to change it are therefore different questions. The same object mechanism underlies execution environments and the closures in the next chapter.

> [!CAUTION]
> The C object API is broader than Goat's available syntax. This version has no general property access through `obj.name` or `obj[key]`, nor user constructs for specifying an arbitrary prototype list. Having corresponding model methods does not mean those features are already available in source programs.

<a id="section-9-7"></a>

## 9.7. Bytecode strings and runtime strings

Names and literals reside in the data segment, but the VM operates on objects. On first loading a string, it creates an object and caches it in the process by descriptor index. Later accesses reuse that object. This avoids creating the name `x` again for every `VLOAD`.

`create_string_object` receives a string value along with a buffer-ownership flag. It directly uses a buffer whose ownership is transferred; it copies borrowed characters. A string object created from the bytecode segment is therefore not merely a pointer into that segment.

When its loop ends, the VM releases cache references and invokes the garbage collector. Releasing the cache does not destroy a string still retained by another live object. Lifetime depends on references and reachability, rather than on which instruction first loaded the string.
