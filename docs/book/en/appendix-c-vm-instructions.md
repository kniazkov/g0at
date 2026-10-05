# Appendix C. VM Instructions

[Contents](index.md) · [Русский](../ru/appendix-c-vm-instructions.md) · [Previous](appendix-b-language.md) · [Next](appendix-d-builtins.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-c-1"></a>

## C.1. Format and notation

[bytecode.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/bytecode.h) defines an 8-byte instruction: `opcode` (8 bits), `flags` (8), `arg0` (16), and `arg1` (32). The table lists all 52 codes in [opcodes.h](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/opcodes.h) order. Handlers are in [vm.c](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/src/vm/vm.c). Numeric codes belong to this format revision, not a promise that the enum will never change.

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
| `0x0E` | `VLOAD` | arg1: name id | S → S, value | Find the name in context and prototypes; use null if absent. |
| `0x0F` | `VAR` | arg1: name id | S, x → S | Create a mutable property in the current context. |
| `0x10` | `CONST` | arg1: name id | S, x → S | Create a constant property in the current context. |
| `0x11` | `STORE` | arg1: name id | S, x → S, x | Write an existing name or create a local; preserve the assignment result. |
| `0x12` | `UPLUS` | — | S, x → S, result | Unary plus. |
| `0x13` | `UMINUS` | — | S, x → S, result | Numeric negation. |
| `0x14` | `INC` | — | S, x → S, result | Increment the numeric value by 1. |
| `0x15` | `DEC` | — | S, x → S, result | Decrement the numeric value by 1. |
| `0x16` | `LNOT` | — | S, x → S, result | Negate truthiness; return boolean. |
| `0x17` | `BOOL` | — | S, x → S, result | Convert truthiness to boolean. |
| `0x18` | `BNOT` | — | S, x → S, result | Integer bitwise complement. |
| `0x19` | `LAND` | arg1: target | S, x → S / S, false | If false, leave false and jump; otherwise consume x and continue with the right side. |
| `0x1A` | `LOR` | arg1: target | S, x → S / S, true | If true, leave true and jump; otherwise consume x and continue with the right side. |
| `0x1B` | `BAND` | — | S, a, b → S, result | Compute `a & b` through object methods. |
| `0x1C` | `BOR` | — | S, a, b → S, result | Compute `a \| b` through object methods. |
| `0x1D` | `BXOR` | — | S, a, b → S, result | Compute `a ^ b` through object methods. |
| `0x1E` | `SHL` | — | S, a, b → S, result | Compute `a << b` through object methods. |
| `0x1F` | `SHR` | — | S, a, b → S, result | Compute `a >> b` through object methods. |
| `0x20` | `ADD` | — | S, a, b → S, result | Compute `a + b` through object methods. |
| `0x21` | `SUB` | — | S, a, b → S, result | Compute `a - b` through object methods. |
| `0x22` | `MUL` | — | S, a, b → S, result | Compute `a * b` through object methods. |
| `0x23` | `DIVIDE` | — | S, a, b → S, result | Compute `a / b` through object methods. |
| `0x24` | `MODULO` | — | S, a, b → S, result | Compute `a % b` through object methods. |
| `0x25` | `POWER` | — | S, a, b → S, result | Compute `a ** b` through object methods. |
| `0x26` | `LESS` | — | S, a, b → S, result | Compute `a < b` through object methods. |
| `0x27` | `LEQ` | — | S, a, b → S, result | Compute `a <= b` through object methods. |
| `0x28` | `GREATER` | — | S, a, b → S, result | Compute `a > b` through object methods. |
| `0x29` | `GREQ` | — | S, a, b → S, result | Compute `a >= b` through object methods. |
| `0x2A` | `EQUAL` | — | S, a, b → S, result | Compute `a == b` through object methods. |
| `0x2B` | `DIFF` | — | S, a, b → S, result | Compute `a != b` through object methods. |
| `0x2C` | `FUNC` | arg0: count; arg1: names id; ARG: body | S → S, function | Create a function with the current environment and optional native descriptor; clear ARG buffer. |
| `0x2D` | `CALL` | arg0: count | S, argN, …, arg1, function → S, result | Pop the callable; pass arguments to its call method. A bytecode result arrives on return. |
| `0x2E` | `RET` | — | frame, x → caller, x | Put x in the reserved result slot; unwind contexts to the function and return to the saved address. |
| `0x2F` | `ENTER` | — | S → S | Create a nested context. |
| `0x30` | `LEAVE` | — | S → S, object | Destroy the current context, retaining and pushing its data object. |
| `0x31` | `RESTORE` | — | S → S | Destroy one context without a result; the root cannot be restored. |
| `0x32` | `TRY` | arg1: handler | S → S | Create an exception context with handler address and stack boundary. |
| `0x33` | `THROW` | — | S, exception → saved S, exception | Pop the value and unwind contexts and stack to a handler; without one, stop the VM with failure. |

<a id="section-c-3"></a>

## C.3. Conditions for reading the table

`INC` and `DEC` do not write a variable themselves: combinations with `DUP` and `STORE` implement prefix or postfix forms. `STORE` preserves the top value; an expression statement may later remove it with a separate `POP`. `LEAVE` and `RESTORE` are likewise not interchangeable.

`ILOAD64` and `RLOAD` refer to the current platform's `split64_t` parts, not a universal network word order. The ordinary binary loader requires `ARG` immediately before these instructions and `FUNC`.

An operation returning an exception does not follow the table's normal stack transition: its value goes to the exception-dispatch mechanism. A successful native `CALL` immediately leaves a result; a bytecode call creates a context and reserves space until `RET`. The default reserved result is `null`.

> [!CAUTION]
> This is a reference for executable instructions, not permission to feed arbitrary sequences to the VM. Persisted `flags` must be zero; the loader does not fully verify stack depth and control flow. Insufficient stack objects cause fatal termination rather than a catchable Goat exception.

Chapter 8 describes instruction construction, chapter 10 contexts, chapter 12 exceptions, and chapter 26 persistence and file validation.
