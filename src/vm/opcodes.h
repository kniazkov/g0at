/**
 * @file opcodes.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the opcodes for the Goat virtual machine.
 *
 * This file contains the definition of the `opcode_t` enumeration, which enumerates all the
 * opcodes used by the Goat virtual machine. Each opcode corresponds to a specific instruction
 * that can be executed by the virtual machine.
 *
 * The opcodes are essential for the virtual machine's operation and define the low-level commands 
 * that are interpreted and executed during the bytecode execution. Each opcode may have associated 
 * arguments, which are either pushed onto the argument stack or directly used in the instruction.
 *
 * @note Modifying the opcodes in this file may affect the virtual machine's instruction set and 
 *  the bytecode format. Changes should be made carefully and with consideration for backward 
 *  compatibility with existing bytecode files.
 *
 * @see bytecode.h for bytecode structure definitions.
 */

#pragma once

/**
 * @enum opcode_t
 * @brief Enumeration of available opcodes for the Goat virtual machine.
 *
 * Each opcode corresponds to a specific instruction that can be executed by the virtual machine.
 * These opcodes define the operations that can be performed by the virtual machine, such as 
 * arithmetic operations, control flow operations, and stack manipulations.
 */
typedef enum {
    /**
     * @brief No operation - does nothing.
     *
     * The `NOP` opcode is used when no operation is required.
     * This opcode can be used for padding or in debugging situations where a placeholder
     * instruction is needed. It does not alter the virtual machine's state.
     */
    NOP = 0x00, /**< No operation - does nothing. */

    /**
     * @brief Argument push onto the argument stack.
     *
     * The `ARG` opcode pushes an argument onto the argument stack.
     * Since each instruction has only two arguments (one 32-bit and one 16-bit), any additional
     * arguments or larger-sized arguments (e.g., 64-bit) must be pushed onto the argument stack
     * using this opcode. When an instruction needs more than the two available arguments,
     * the additional arguments are pushed onto the stack in reverse order, and the target
     * instruction will pop them as needed.
     */
    ARG, /**< Adds an argument to the argument stack. */

    /**
     * @brief Ends the program immediately.
     *
     * The `END` opcode signals the virtual machine to terminate the program immediately. Once this 
     * instruction is encountered, the VM halts, and no further instructions are executed. It is 
     * used for normal program termination.
     */
    END, /**< Immediately ends the program execution. */

    /**
     * @brief Pops an object off the data stack.
     *
     * The `POP` opcode removes the top object from the data stack. This is used when an object 
     * is no longer needed and should be discarded.
     */
    POP, /**< Removes the top object from the data stack. */

    /**
     * @brief Pushes a null object onto the data stack.
     *
     * The `NIL` opcode pushes a null object onto the data stack. This is used to represent
     * the absence of a value or as a default placeholder. The name `NIL` is used instead of
     * `NULL` to avoid confusion with the C language's NULL macro.
     */
    NIL, /**< Pushes a null object onto the data stack. */

    /**
     * @brief Pushes a 32-bit integer onto the data stack.
     *
     * The `ILOAD32` opcode pushes a 32-bit integer onto the data stack. This is used for loading
     * integer values into the virtual machine's stack for further operations.
     */
    ILOAD32, /**< Pushes a 32-bit integer onto the data stack. */

    /**
     * @brief Pushes a 64-bit integer onto the data stack.
     *
     * The `ILOAD64` opcode pushes a 64-bit integer onto the data stack. Since the virtual machine 
     * can only store 32-bit integers in a single instruction, the 64-bit value must be pushed in 
     * two parts: the lower 32 bits can be pushed with `ILOAD32`, and the upper 32 bits are loaded 
     * separately using the `ARG` instruction.
     */
    ILOAD64, /**< Pushes a 64-bit integer onto the data stack. */

    /**
     * @brief Pushes a 64-bit floating-point number onto the data stack.
     *
     * The `RLOAD` opcode is used to push a 64-bit double-precision float onto the data stack.
     * Like `ILOAD64`, it requires two instructions: the high 32 bits are encoded in the `arg1`
     * field of `RLOAD`, and the low 32 bits must be pushed beforehand using the `ARG` opcode.
     * This ensures proper reconstruction of the full 64-bit floating-point value.
     */
    RLOAD, /**< Pushes a 64-bit float onto the data stack (requires ARG for low bits). */

    /**
     * @brief Loads a static string onto the data stack.
     *
     * The `SLOAD` opcode pushes a static string object onto the data stack. Static strings are
     * predefined and have a fixed memory location (in the bytecode), meaning they are not subject
     * to garbage collection. 
     */
    SLOAD, /**< Loads a static string onto the data stack. */

    /**
     * @brief Loads the value of a variable from the current context onto the data stack.
     * 
     * The `VLOAD` opcode retrieves the value of a variable from the current execution context, 
     * identified by its name (static string), and pushes it onto the data stack. If the variable 
     * does not exist in the current context, the constant `null` is loaded instead.
     */
    VLOAD, /**< Loads a variable value onto the data stack or `null` if undefined. */

    /**
     * @brief Declares a new mutable variable in the current execution context.
     *
     * The `VAR` opcode creates a new mutable variable binding in the current context.
     * Behavior:
     * - If variable/constant already exists in current context: throws an exception
     * - If variable/constant exists in parent context: creates new binding in current context
     *   (shadowing)
     * - Otherwise: creates new variable in current context
     */
    VAR, /**< Declares a new mutable variable in current context. */

    /**
     * @brief Declares a new immutable constant in the current execution context.
     *
     * The `CONST` opcode creates a new immutable binding in the current context.
     * Behavior:
     * - If variable/constant already exists in current context: throws an exception
     * - If variable/constant exists in parent context: creates new binding in current context
     *   (shadowing)
     * - Otherwise: creates new constant in current context
     * Unlike variables declared with VAR, constants:
     * - Cannot be modified after creation (attempt throws exception)
     * - Can still be shadowed in child contexts
     */
    CONST, /**< Declares a new immutable constant in current context. */

    /**
     * @brief Stores a value to an existing variable in the context chain.
     *
     * The `STORE` opcode updates a variable's value searching through the context chain.
     * Behavior:
     * - Searches for variable starting from current context up through parents
     * - If found and mutable: updates the value
     * - If found but constant: throws an exception
     * - If not found: creates new variable in current context (like VAR)
     * This enables closures by allowing inner functions to modify outer scopes.
     */
    STORE, /**< Stores to existing variable or creates new if not found. */

   /**
     * @brief Adds the top two objects of the stack.
     *
     * The `ADD` opcode performs an addition operation on the two topmost objects on the data stack. 
     * It pops the top two objects, adds them, and then pushes the result back onto the stack. This 
     * operation can be used for both numerical and non-numerical objects, depending on the virtual 
     * machine's behavior.
     */
    ADD, /**< Adds the top two objects on the data stack. */

    /**
     * @brief Subtracts the top two objects of the stack.
     *
     * The `SUB` opcode performs a subtraction operation on the two topmost objects on the
     * data stack. It pops the top two objects, subtracts the second operand from the first,
     * and then pushes the result back onto the stack. This operation can be used for numerical
     * objects or other types that support subtraction.
     */
    SUB, /**< Subtracts the top two objects on the data stack. */

    /**
     * @brief Creates a new function object.
     *
     * The `FUNC` opcode constructs a new callable function object. It requires three arguments:
     * 1. 16-bit argument count (immediate value)
     * 2. 32-bit reference to argument names array (in data segment)
     * 3. 32-bit entry point address (passed via ARG instruction)
     *
     * Usage in bytecode always appears as an ARG+FUNC pair.
     */
    FUNC, /**< Creates a new function object. */

    /**
     * @brief Calls a function with arguments from the data stack.
     *
     * The `CALL` opcode interprets the object at the top of the stack as a function and invokes it.
     * Before calling, it expects the arguments for the function to be already present on the stack
     * in the correct order. The number of arguments is specified as the parameter to the `CALL`
     * instruction.
     * 
     * Upon execution, the `CALL` instruction:
     * - Pops the specified number of arguments from the stack.
     * - Pops the function object to be invoked.
     * - Calls the function with the provided arguments.
     * - Pushes the result of the function onto the stack.
     * 
     * If the function does not return a value, a `null` object is pushed onto the stack instead.
     *
     * This opcode facilitates invoking callable objects within the virtual machine, allowing
     * for dynamic function calls and composition.
     */
    CALL, /**< Calls a function with arguments from the data stack. */

    /**
     * @brief Returns from the current function.
     *
     * The `RET` opcode terminates execution of the current function and returns control
     * to the caller. It expects the return value (or `null` as a value) to be on top of the stack.
     *
     * Every function implicitly ends with RET if no explicit return exists.
     */
    RET, /**< Returns from current function. */

    /**
     * @brief Creates a new execution context using the current context as a prototype.
     *
     * The `ENTER` opcode establishes a new execution context, inheriting variables and constants
     * from the current context (prototype). All subsequent variable/constant operations
     * (`VAR`, `CONST`, `STORE`, `VLOAD`) will refer to this new context. The previous context is
     * preserved and can be restored with `LEAVE`.
     */
    ENTER, /**< Creates a new context, inheriting from the current one. */

    /**
     * @brief Restores the previous execution context.
     *
     * The `LEAVE` opcode exits the current context, reverting to the previous one. The abandoned
     * context remains on the stack (unless explicitly popped), allowing it to be stored
     * for later use.
     */
    LEAVE /**< Restores the parent context, leaving the current one on the stack. */
} opcode_t;
