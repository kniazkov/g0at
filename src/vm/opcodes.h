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
    ARG = 0x01, /**< Adds an argument to the argument stack. */

    /**
     * @brief Ends the program immediately.
     *
     * The `END` opcode signals the virtual machine to terminate the program immediately. Once this 
     * instruction is encountered, the VM halts, and no further instructions are executed. It is 
     * used for normal program termination.
     */
    END = 0x02, /**< Immediately ends the program execution. */

    /**
     * @brief Pops an object off the data stack.
     *
     * The `POP` opcode removes the top object from the data stack. This is used when an object 
     * is no longer needed and should be discarded.
     */
    POP = 0x03, /**< Removes the top object from the data stack. */

    /**
     * @brief Pushes a 32-bit integer onto the data stack.
     *
     * The `ILOAD32` opcode pushes a 32-bit integer onto the data stack. This is used for loading
     * integer values into the virtual machine's stack for further operations.
     */
    ILOAD32 = 0x04, /**< Pushes a 32-bit integer onto the data stack. */

    /**
     * @brief Pushes a 64-bit integer onto the data stack.
     *
     * The `ILOAD64` opcode pushes a 64-bit integer onto the data stack. Since the virtual machine 
     * can only store 32-bit integers in a single instruction, the 64-bit value must be pushed in 
     * two parts: the lower 32 bits can be pushed with `ILOAD32`, and the upper 32 bits are loaded 
     * separately using the `ARG` instruction.
     */
    ILOAD64 = 0x05, /**< Pushes a 64-bit integer onto the data stack. */

    /**
     * @brief Adds the top two objects of the stack.
     *
     * The `ADD` opcode performs an addition operation on the two topmost objects on the data stack. 
     * It pops the top two objects, adds them, and then pushes the result back onto the stack. This 
     * operation can be used for both numerical and non-numerical objects, depending on the virtual 
     * machine's behavior.
     */
    ADD = 0x06, /**< Adds the top two objects on the data stack. */

    /**
     * @brief Subtracts the top two objects of the stack.
     *
     * The `SUB` opcode performs a subtraction operation on the two topmost objects on the
     * data stack. It pops the top two objects, subtracts the second operand from the first,
     * and then pushes the result back onto the stack. This operation can be used for numerical
     * objects or other types that support subtraction.
     */
    SUB = 0x07, /**< Subtracts the top two objects on the data stack. */
} opcode_t;
