# 8. Bytecode Generation

[Contents](index.md) · [Русский](../ru/08-bytecode-generation.md) · [Previous chapter](07-ast-and-name-binding.md) · [Next chapter](09-vm-and-values.md)

Revision 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-8-1"></a>

## 8.1. From a tree to a sequence of actions

An AST reads naturally as a description of a program: addition has two operands, and a conditional has two branches. An executor needs another form: obtain the values, add them, and pass the result onward. In Goat, bytecode (instructions for its own virtual machine) specifies that sequence.

An AST node invokes generation methods on its children and appends its own instructions. The tree therefore determines execution order. The generator receives an analyzed tree, possibly simplified by the optimizer, and produces instructions and data. Bytecode contains no pointers to AST nodes. The tree can be freed after generation.

The examples below use `--optimize none` to expose this stage: otherwise, constant evaluation may remove some operations. Name binding still takes place. The bytecode assembly path is in [launcher.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/cli/launcher.c); node methods are in `src/graph`.

<a id="section-8-2"></a>

## 8.2. Two builders

[code_builder.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/code_builder.c) accumulates a dynamic instruction array. Appending returns an index, or instruction number. A VM jump address is such an index, rather than a machine memory address or a byte offset. The array grows, so a pointer to an element must not be kept across further appends; the generator obtains the element again by index when patching a jump.

[data_builder.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/data_builder.c) separately accumulates bytes and descriptors (records locating data blocks). A descriptor contains an offset and a size. Instructions refer to data by descriptor number. New blocks are aligned to four-byte boundaries. Strings are null-terminated sequences of `wchar_t`; equal strings reuse an entry through an AVL-tree table.

The segment contains more than string literals. It also holds variable names and arrays of string indices identifying function parameters. Thus, `VLOAD` receives the index of a name string, rather than a local register number.

<a id="section-8-3"></a>

## 8.3. What fits in an instruction

In [bytecode.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/bytecode.h), `instruction_t` occupies eight bytes:

| Field | Size | Purpose |
|---|---:|---|
| `opcode` | 1 byte | Operation kind |
| `flags` | 1 byte | Format flag field |
| `arg0` | 2 bytes | For example, a call's argument count |
| `arg1` | 4 bytes | For example, a number, jump address, or data index |

Not every instruction uses every field. `ADD` takes values from the stack. `ILOAD32` treats `arg1` as a signed 32-bit number and creates a language integer value. A 64-bit integer or `double` uses two 32-bit parts: a preceding `ARG` supplies one, and `ILOAD64` or `RLOAD` supplies the other.

`ARG` stores auxiliary operands in a separate thread buffer. Despite its name, it does not pass a Goat function argument: language values travel through the object stack. The same `ARG` mechanism supplies the body address to `FUNC`. The operation list is in [opcodes.h](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/opcodes.h).

<a id="section-8-4"></a>

## 8.4. An expression leaves a value

Consider `x + 2`. The variable node emits `VLOAD`, the literal emits `ILOAD32`, and addition emits `ADD`. The stack (last in, first out) changes as follows; its deeper portion is on the left:

| Instruction | Stack before | Stack after |
|---|---|---|
| `VLOAD "x"` | `…` | `…, x` |
| `ILOAD32 2` | `…, x` | `…, x, 2` |
| `ADD` | `…, x, 2` | `…, x + 2` |

The quoted string is explanatory: the actual instruction holds a data index. A binary operation generates its left operand before its right; the executor pops the right, then the left. For subtraction, reversing them would change the result.

An expression statement whose result is no longer needed ends with `POP`. A declaration consumes its value through `VAR` or `CONST`. Assignment uses `STORE`, keeping the result on the stack: this permits assignment inside another expression. A return emits its expression followed by `RET`; for `return;`, `NIL` supplies the value.

<a id="section-8-5"></a>

## 8.5. A conditional becomes addresses

[if_else.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/if_else.c) first generates the condition and a `JIF` whose target is not yet filled in. `JIF` pops a value and jumps when it is false. The true branch follows.

If there is an `else`, a `JUMP` after the true branch skips the false branch. Its first index is now known and is written into `JIF`. After generating the false branch, the continuation address is known and is written into `JUMP`. Without an `else`, the false jump goes directly to the continuation.

Filling in addresses after generation is often called *backpatching*. Here it literally updates a field in the builder. Nested conditionals apply the same technique independently. Short-circuit `&&` and `||` also need jumps: the right operand must not run when the left already determines the result.

<a id="section-8-6"></a>

## 8.6. Creating a function does not execute its body

In [function_object.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/function_object.c), a `func` expression first emits an `ARG` with a placeholder address and a `FUNC` with the parameter count and a reference to their names. This pair creates a function object at runtime. The body is appended later, after the main code and `END`.

The launcher visits the function list, generates available bodies, and patches addresses in the earlier `ARG` instructions. It repeats the pass: generating an outer body may create the first instruction site for an inner function. An empty body gets `NIL; RET`; the same implicit return is added if its final AST statement is not a `return`.

A call is separate. [function_call.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/graph/function_call.c) generates arguments right to left, then the callee expression and `CALL`. The actual argument count goes into `arg0`. This lets the executor pop arguments in parameter order. Deferred body generation does not mean deferred argument evaluation.

<a id="section-8-7"></a>

## 8.7. One complete example

File [08-bytecode.goat](../examples/08-bytecode.goat):

```goat
const choose = func(x) {
    if (x < 0) { return 0; }
    return x + 2;
};
println(choose(3));
```

From the repository root:

```sh
./goat --optimize none --print-bytecode docs/book/examples/08-bytecode.goat
```

```powershell
.\goat.exe --optimize none --print-bytecode .\docs\book\examples\08-bytecode.goat
```

Below is the same listing in compact notation: every index is explicit, and names aid reading. Descriptor numbers belong to this particular example.

```text
 0  ARG 10
 1  FUNC 1, 1
 2  CONST 2          "choose"
 3  ILOAD32 3
 4  VLOAD 2          "choose"
 5  CALL 1
 6  VLOAD 3          "println"
 7  CALL 1
 8  POP
 9  END
10  VLOAD 0          "x"
11  ILOAD32 0
12  LESS
13  JIF 19
14  ENTER
15  ILOAD32 0
16  RET
17  LEAVE
18  POP
19  VLOAD 0          "x"
20  ILOAD32 2
21  ADD
22  RET
```

The `CALL` at 5 transfers control to the body at 10. With `x = 3`, the condition is false, so `JIF` goes to 19. The return supplies `5` to the outer `println` call. After printing, `POP` removes the returned `null`, and `END` finishes the main code. The `LEAVE` and `POP` at 17–18 are unreachable here: `RET` runs first. Generation without optimization may leave such instructions in place.

<a id="section-8-8"></a>

## 8.8. Joining code and data

[linker.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/codegen/linker.c) allocates a combined buffer containing a header, instructions, descriptors, and data. A `bytecode_t` stores pointers to its sections and element counts. The builders are no longer needed after copying. Native preparation may attach additional descriptors to individual `FUNC` sites; these are not instruction bytes.

This internal image is distinct from the saved `.gbin` format handled by [binary.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/vm/binary.c), which part V will cover.

> [!CAUTION]
> The internal representation depends on platform data sizes and representations, including `wchar_t`. Eight bytes per instruction do not by themselves make the entire image portable across platforms.

The virtual machine executes bytecode. Producing processor machine code is the native backend’s task, which the book turns to in part V.
