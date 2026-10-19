# Appendix C. VM Instructions

[Contents](index.md) · [Русский](../ru/appendix-c-vm-instructions.md) · [Previous](appendix-b-language.md) · [Next](appendix-d-builtins.md)

Revision 4. Implementation described: [commit ae26b84, method calls and the `this` constant](https://github.com/kniazkov/g0at/tree/ae26b849471233f3329214f3c170a7a26a756118).

<a id="section-c-1"></a>

## C.1. Format and notation

[bytecode.h](https://github.com/kniazkov/g0at/blob/ae26b849471233f3329214f3c170a7a26a756118/src/vm/bytecode.h) defines an 8-byte instruction: `opcode` (8 bits), `flags` (8), `arg0` (16), and `arg1` (32). The table lists all 56 codes in [opcodes.h](https://github.com/kniazkov/g0at/blob/ae26b849471233f3329214f3c170a7a26a756118/src/vm/opcodes.h) order. Handlers are in [vm.c](https://github.com/kniazkov/g0at/blob/ae26b849471233f3329214f3c170a7a26a756118/src/vm/vm.c). Numeric codes belong to this format revision, not a promise that the enum will never change.

`S` is the retained stack prefix; the top is on the right. `result` is a normal result without an exception. `target`, `body`, and `handler` are instruction indices; `data id` / `name id` are data-descriptor indices, not pointers. Unused operands are `—`. The auxiliary `ARG` buffer is not the object stack.

<a id="section-c-2"></a>

## C.2. Complete list

| Code | Instruction | Operands | Stack | Action |
| --- | --- | --- | --- | --- |
| `0x00` | `NOP` | — | S → S | Continue at the next instruction. |
| `0x01` | `ARG` | arg1: word | S → S | Append a 32-bit word to the thread auxiliary operand buffer. |
| `0x02` | `END` | — | S → S | Stop the VM loop. |
| `0x03` | `JUMP` | arg1: target | S → S | Set the instruction index unconditionally. |
| `0x04` | `JIF` | arg1: target | S, x → S | Jump if x is false; otherwise continue. Both paths consume it. |
| `0x05` | `POP` | — | S, x → S | Release the popped reference. |
| `0x06` | `DUP` | — | S, x → S, x, x | Retain another reference to the top value. |
| `0x07` | `NIL` | — | S → S, null | Push the shared value object. |
| `0x08` | `TRUE` | — | S → S, true | Push the shared value object. |
| `0x09` | `FALSE` | — | S → S, false | Push the shared value object. |
| `0x0A` | `ILOAD32` | arg1: bits | S → S, integer | Interpret arg1 as int32 and create an integer. |
| `0x0B` | `ILOAD64` | ARG + arg1 | S → S, integer | Assemble int64 from ARG parts[0] and arg1 parts[1]; clear the buffer. |
| `0x0C` | `RLOAD` | ARG + arg1 | S → S, real | Assemble double bits as for ILOAD64; this is not numeric conversion. |
| `0x0D` | `SLOAD` | arg1: data id | S → S, string | Load a string through the process cache. |
| `0x0E` | `CLOAD` | arg1: code | S → S, character | Create a character from the given code. |
| `0x0F` | `VLOAD` | arg1: name id | S → S, value | Find the name in context and prototypes; use null if absent. |
| `0x10` | `VAR` | arg1: name id | S, x → S | Create a mutable property in the current context. |
| `0x11` | `CONST` | arg1: name id | S, x → S | Create a constant property in the current context. |
| `0x12` | `STORE` | arg1: name id | S, x → S, x | Write an existing name or create a local; preserve the assignment result. |
| `0x13` | `GETPROP` | arg1: name id | S, obj → S, value | Pop the object and push its named property, or null if missing. |
| `0x14` | `SETPROP` | arg1: name id | S, value, obj → S, value | Pop the object and set its named property to the value below it. |
| `0x15` | `UPLUS` | — | S, x → S, result | Unary plus. |
| `0x16` | `UMINUS` | — | S, x → S, result | Numeric negation. |
| `0x17` | `INC` | — | S, x → S, result | Increment the numeric value by 1. |
| `0x18` | `DEC` | — | S, x → S, result | Decrement the numeric value by 1. |
| `0x19` | `LNOT` | — | S, x → S, result | Negate truthiness; return boolean. |
| `0x1A` | `BOOL` | — | S, x → S, result | Convert truthiness to boolean. |
| `0x1B` | `BNOT` | — | S, x → S, result | Integer bitwise complement. |
| `0x1C` | `LAND` | arg1: target | S, x → S / S, false | If false, leave false and jump; otherwise consume x and continue with the right side. |
| `0x1D` | `LOR` | arg1: target | S, x → S / S, true | If true, leave true and jump; otherwise consume x and continue with the right side. |
| `0x1E` | `BAND` | — | S, a, b → S, result | Compute `a & b` through object methods. |
| `0x1F` | `BOR` | — | S, a, b → S, result | Compute `a \| b` through object methods. |
| `0x20` | `BXOR` | — | S, a, b → S, result | Compute `a ^ b` through object methods. |
| `0x21` | `SHL` | — | S, a, b → S, result | Compute `a << b` through object methods. |
| `0x22` | `SHR` | — | S, a, b → S, result | Compute `a >> b` through object methods. |
| `0x23` | `ADD` | — | S, a, b → S, result | Compute `a + b` through object methods. |
| `0x24` | `SUB` | — | S, a, b → S, result | Compute `a - b` through object methods. |
| `0x25` | `MUL` | — | S, a, b → S, result | Compute `a * b` through object methods. |
| `0x26` | `DIVIDE` | — | S, a, b → S, result | Compute `a / b` through object methods. |
| `0x27` | `MODULO` | — | S, a, b → S, result | Compute `a % b` through object methods. |
| `0x28` | `POWER` | — | S, a, b → S, result | Compute `a ** b` through object methods. |
| `0x29` | `LESS` | — | S, a, b → S, result | Compute `a < b` through object methods. |
| `0x2A` | `LEQ` | — | S, a, b → S, result | Compute `a <= b` through object methods. |
| `0x2B` | `GREATER` | — | S, a, b → S, result | Compute `a > b` through object methods. |
| `0x2C` | `GREQ` | — | S, a, b → S, result | Compute `a >= b` through object methods. |
| `0x2D` | `EQUAL` | — | S, a, b → S, result | Compute `a == b` through object methods. |
| `0x2E` | `DIFF` | — | S, a, b → S, result | Compute `a != b` through object methods. |
| `0x2F` | `FUNC` | arg0: count; arg1: names id; ARG: body | S → S, function | Create a function with the current environment and optional native descriptor; clear ARG buffer. |
| `0x30` | `CALL` | arg0: count | S, arg1, …, argN, function → S, result | Pop the callable; pass arguments to its call method. A bytecode result arrives on return. |
| `0x31` | `CALLM` | arg0: count | S, arg1, …, argN, receiver, function → S, result | Pop the function and the receiver below it; pass arguments and the receiver to the function's call method. |
| `0x32` | `RET` | — | frame, x → caller, x | Put x in the reserved result slot; unwind contexts to the function and return to the saved address. |
| `0x33` | `ENTER` | — | S → S | Create a nested context. |
| `0x34` | `LEAVE` | — | S → S, object | Destroy the current context, retaining and pushing its data object. |
| `0x35` | `RESTORE` | — | S → S | Destroy one context without a result; the root cannot be restored. |
| `0x36` | `TRY` | arg1: handler | S → S | Create an exception context with handler address and stack boundary. |
| `0x37` | `THROW` | — | S, exception → saved S, exception | Pop the value and unwind contexts and stack to a handler; without one, stop the VM with failure. |

<a id="section-c-3"></a>

## C.3. Conditions for reading the table

`INC` and `DEC` do not write a variable themselves: combinations with `DUP` and `STORE` implement prefix or postfix forms. `STORE` preserves the top value; an expression statement may later remove it with a separate `POP`. `LEAVE` and `RESTORE` are likewise not interchangeable.

`ILOAD64` and `RLOAD` refer to the current platform's `split64_t` parts, not a universal network word order. The ordinary binary loader requires `ARG` immediately before these instructions and `FUNC`.

An operation returning an exception does not follow the table's normal stack transition: its value goes to the exception-dispatch mechanism. A successful native `CALL` immediately leaves a result; a bytecode call creates a context and reserves space until `RET`. The default reserved result is `null`.

> [!CAUTION]
> This is a reference for executable instructions, not permission to feed arbitrary sequences to the VM. Persisted `flags` must be zero; the loader does not fully verify stack depth and control flow. Insufficient stack objects cause fatal termination rather than a catchable Goat exception.

Chapter 8 describes instruction construction, chapter 10 contexts, chapter 12 exceptions, and chapter 26 persistence and file validation.

## GETINDEX / SETINDEX

| Opcode | arg1 | Stack | Virtual method |
| --- | --- | --- | --- |
| `GETINDEX` | index count (uint32) | S, receiver, indices → S, result | `get_index(process, receiver, indices)` |
| `SETINDEX` | index count (uint32) | S, value, receiver, indices → S, result | `set_index(process, receiver, indices, value)` |

Methods borrow their operands and return an owned result or exception. The VM releases operands and dispatches exceptions normally. The concrete type validates index count, not the VM. New opcodes are appended without renumbering existing instructions.

## ARRAY

`arg1` is the element count (uint32). Stack: `S, element0, …, elementN → S, array`.
The instruction preserves source order, retains elements and creates a fresh array;
`ARRAY 0` creates an empty array. Insufficient stack elements are a fatal bytecode
error. `GETINDEX` / `SETINDEX` dispatch to the array methods.
