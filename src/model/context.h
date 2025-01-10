/**
 * @file context.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure and behavior for the execution context in the
 *  Goat programming language.
 * 
 * In the Goat programming language, the concept of a "context" refers to the set of data
 * and control information that is needed for the execution of a specific part of a program.
 * A context encapsulates the current state of execution, including variable values, execution flow,
 * and other necessary data.
 * 
 * The context is critical for maintaining the flow of control during program execution.
 * For example, during a function call, a new context is created that holds the function's local
 * variables and return address, and the current context is pushed onto a stack to preserve the
 * execution state. When the function execution completes, the previous context is popped
 * from the stack, restoring the previous execution state.
 */

#pragma once

/**
 * @typedef object_t
 * @brief Forward declaration for the object structure.
 */
typedef struct object_t object_t;

/**
 * @struct context_t
 * @brief Represents the execution context in the Goat programming language.
 * 
 * A context in Goat encapsulates all the data that is needed to execute a particular segment
 * of the program. It includes references to the current set of data (i.e., the values of variables)
 * and any additional control information, such as function return addresses, that may be required
 * for managing program flow.
 */
typedef struct {
    /**
     * @brief A data associated with the current execution context.
     */
    object_t *data;

    // Other fields (such as program counter, return address, etc.) can be added later.
} context_t;

/**
 * @brief Retrieves the singleton instance of the root context.
 * 
 * This function provides access to the root context of the program. The root context 
 * contains the built-in objects and functions that are available from the start of the program.
 * It is a singleton, and the same instance is returned every time this function is called.
 * 
 * @return A pointer to the singleton instance of the root context.
 */
context_t *get_root_context();
