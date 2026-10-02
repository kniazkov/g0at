/**
 * @file opcodes.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the opcodes for the Goat virtual machine.
 */

#pragma once

/** @brief Enumeration of available opcodes for the Goat virtual machine. */
typedef enum {
    NOP = 0x00, /**< No operation - does nothing. */

    ARG, /**< Pushes an auxiliary operand; consumed by a subsequent instruction. */

    END, /**< Immediately ends the program execution. */

    JUMP, /**< Unconditionally jumps to another instruction. */

    JIF, /**< Jumps to another instruction if the stack contains `false`. */

    POP, /**< Removes the top object from the data stack. */

    NIL, /**< Pushes a null object onto the data stack. */

    TRUE, /**< Pushes the boolean value true onto the data stack. */

    FALSE, /**< Pushes the boolean value false onto the data stack. */

    ILOAD32, /**< Pushes a 32-bit integer onto the data stack. */

    ILOAD64, /**< Loads split64_t parts from ARG and arg1. */

    RLOAD, /**< Loads double bits from split64_t parts in ARG and arg1. */

    SLOAD, /**< Loads a static string onto the data stack. */

    VLOAD, /**< Loads a variable value onto the data stack or `null` if undefined. */

    VAR, /**< Declares a new mutable variable in current context. */

    CONST, /**< Declares a new immutable constant in current context. */

    STORE, /**< Stores to existing variable or creates new if not found. */

    UPLUS, /**< Applies unary plus to the top value. */

    UMINUS, /**< Negates the top value. */

    ADD, /**< Adds the top two objects on the data stack. */

    SUB, /**< Subtracts the top two objects on the data stack. */

    MUL, /**< Multiplies the top two objects on the data stack. */

    DIVIDE, /**< Divides the first object by the second on the data stack. */

    MODULO, /**< Computes the modulo of the top two objects on the data stack. */

    POWER, /**< Raises the first object to the power of the second. */

    LESS, /**< Checks if first < second and pushes boolean result. */

    LEQ, /**< Checks if first <= second and pushes boolean result. */

    GREATER, /**< Checks if first > second and pushes boolean result. */

    GREQ, /**< Checks if first >= second and pushes boolean result. */

    EQUAL, /**< Pushes `true` if top two objects are equal. */

    DIFF, /**< Pushes `true` if top two objects are not equal. */

    FUNC, /**< Creates a new function object. */

    CALL, /**< Calls a function with arguments from the data stack. */

    RET, /**< Returns from current function. */

    ENTER, /**< Creates a new context, inheriting from the current one. */

    LEAVE, /**< Restores the parent context, pushing the departed context's data. */

    RESTORE, /**< Destroys one context without changing the data stack. */

    TRY, /**< Creates an exception context; arg1 is the handler address. */

    THROW /**< Transfers the top value to the nearest handler after unwinding. */
} opcode_t;
