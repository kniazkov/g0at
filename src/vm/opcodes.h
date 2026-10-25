/**
 * @file opcodes.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the opcodes for the Goat virtual machine.
 */

#pragma once

/** @brief Enumeration of available opcodes for the Goat virtual machine. */
typedef enum {
    NOP = 0x00, /**< No operation - does nothing. */
    ARG,        /**< Pushes an auxiliary operand; consumed by a subsequent instruction. */
    END,        /**< Immediately ends the program execution. */
    JUMP,       /**< Unconditionally jumps to another instruction. */
    JIF,        /**< Jumps to another instruction if the stack contains `false`. */
    POP,        /**< Removes the top object from the data stack. */
    DUP,        /**< Retains and duplicates the top stack value. */
    NIL,        /**< Pushes a null object onto the data stack. */
    TRUE,       /**< Pushes the boolean value true onto the data stack. */
    FALSE,      /**< Pushes the boolean value false onto the data stack. */
    ILOAD32,    /**< Pushes a 32-bit integer onto the data stack. */
    ILOAD64,    /**< Loads split64_t parts from ARG and arg1. */
    RLOAD,      /**< Loads double bits from split64_t parts in ARG and arg1. */
    SLOAD,      /**< Loads a static string onto the data stack. */
    CLOAD,      /**< Loads a character code onto the data stack. */
    VLOAD,      /**< Loads a variable value onto the data stack or `null` if undefined. */
    VAR,        /**< Declares a new mutable variable in current context. */
    CONST,      /**< Declares a new immutable constant in current context. */
    STORE,      /**< Stores to existing variable or creates new if not found. */
    GETPROP,    /**< Pushes the named property of the top object, or `null` if missing. */
    SETPROP,    /**< Stores the value below the top into the named property of the top object. */
    UPLUS,      /**< Applies unary plus to the top value. */
    UMINUS,     /**< Negates the top value. */
    INC,        /**< Replaces the top value with its numeric successor. */
    DEC,        /**< Replaces the top value with its numeric predecessor. */
    LNOT,       /**< Logical negation. */
    BOOL,       /**< Converts the top value to boolean. */
    BNOT,       /**< Integer bitwise inversion. */
    LAND,       /**< Converts and keeps false, jumping to arg1; otherwise pops and continues. */
    LOR,        /**< Converts and keeps true, jumping to arg1; otherwise pops and continues. */
    BAND,       /**< Integer bitwise and. */
    BOR,        /**< Integer bitwise or. */
    BXOR,       /**< Integer bitwise xor. */
    SHL,        /**< Integer shift left. */
    SHR,        /**< Integer shift right. */
    ADD,        /**< Adds the top two objects on the data stack. */
    SUB,        /**< Subtracts the top two objects on the data stack. */
    MUL,        /**< Multiplies the top two objects on the data stack. */
    DIVIDE,     /**< Divides the first object by the second on the data stack. */
    MODULO,     /**< Computes the modulo of the top two objects on the data stack. */
    POWER,      /**< Raises the first object to the power of the second. */
    LESS,       /**< Checks if first < second and pushes boolean result. */
    LEQ,        /**< Checks if first <= second and pushes boolean result. */
    GREATER,    /**< Checks if first > second and pushes boolean result. */
    GREQ,       /**< Checks if first >= second and pushes boolean result. */
    EQUAL,      /**< Pushes `true` if top two objects are equal. */
    DIFF,       /**< Pushes `true` if top two objects are not equal. */
    FUNC,       /**< Creates a new function object. */
    CALL,       /**< Calls a function with arguments from the data stack. */
    CALLM,      /**< Calls a method; the receiver sits on the stack below the function. */
    RET,        /**< Returns from current function. */
    ENTER,      /**< Creates a new context, inheriting from the current one. */
    LEAVE,      /**< Restores the parent context, pushing the departed context's data. */
    RESTORE,    /**< Destroys one context without changing the data stack. */
    TRY,        /**< Creates an exception context; arg1 is the handler address. */
    THROW,      /**< Transfers the top value to the nearest handler after unwinding. */
    GETINDEX,   /**< Consumes receiver and arg1 indices, pushes the indexed value. */
    SETINDEX,   /**< Consumes receiver and arg1 indices above RHS; keeps assignment value. */
    ARRAY,      /**< Consumes arg1 elements in source order and pushes a new array. */
    INHERIT, /**< Enters a derived object: arg0=1 for arg1 explicit prototypes, else one value. */
    LOOP_ENTER,    /**< Opens a loop scope with an operand stack boundary. */
    LOOP_JUMP,     /**< Unwinds to the nearest loop and jumps to arg1. */
    DUPN,          /**< Duplicates the top arg1 values in their original order. */
    ROTATE,        /**< Moves the top value below arg1 retained operands. */
    SNAPSHOT,      /**< Copies array values or sorted own object keys. */
    ITER_NEXT,     /**< Pushes next snapshot value, or jumps to arg1 on exhaustion. */
    FINALLY_ENTER, /**< Creates a protected scope; arg1 is its cleanup address. */
    FINALLY_LEAVE, /**< Starts cleanup on normal exit; arg1 is the continuation. */
    FINALLY_END,   /**< Resumes the transfer suspended by this cleanup. */
    DVAR,
    DCONST, /**< Declare a field using the declaration descriptor at arg1. */
    BLOAD,
    BSTORE, /**< Read/write the lexical declaration identified by arg1. */
    MODULE_INCLUDE =
        73,             /**< Initializes a fixed local file once and pushes its persistent scope. */
    MODULE_IMPORT = 74, /**< Reserved v1 literal module name reference. */
    MODULE_MAIN = 75,   /**< Reserved dynamic main module value. */
    MODULE_DIR = 76,    /**< Reserved dynamic module directory value. */
    CHECKPROP = 77,     /**< Checks named write on top receiver, retaining stack operands. */
    CHECKINDEX = 78,    /**< Checks write on receiver plus arg1 indices, retaining operands. */
    CHECKVAR = 79,      /**< Checks a named lexical write without changing the stack. */
    CHECKBOUND = 80     /**< Checks a declaration-bound write without changing the stack. */
} opcode_t;
