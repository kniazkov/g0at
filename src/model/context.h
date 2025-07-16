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

#include "common_types.h"

/**
 * @typedef object_t
 * @brief Forward declaration for the object structure.
 */
typedef struct object_t object_t;

/**
 * @typedef context_t
 * @brief Forward declaration for the context structure.
 */
typedef struct context_t context_t;

/**
 * @typedef process_t
 * @brief Forward declaration for the process structure.
 */
typedef struct process_t process_t;

/**
 * @struct context_t
 * @brief Represents the execution context in the Goat programming language.
 * 
 * A context in Goat encapsulates all the data that is needed to execute a particular segment
 * of the program. It includes references to the current set of data (i.e., the values of variables)
 * and any additional control information, such as function return addresses, that may be required
 * for managing program flow.
 */
struct context_t {
    /**
     * @brief A data associated with the current execution context.
     */
    object_t *data;

   /**
     * @brief A pointer to the previous context in the stack.
     * 
     * This field allows the formation of a stack of contexts, where each context references
     * the context from which it was created. When the current context is no longer needed,
     * the previous context can be restored.
     */
    context_t *previous;

    /**
     * @brief The instruction address to return to after context completion.
     * 
     * For function calls, stores the location in the bytecode where execution
     * should resume after the function returns. Zero address meaning either:
     *   - This is a top-level context (global scope)
     *   - The return address is stored in the previous contexts
     */
    instr_index_t ret_address;
};

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

/**
 * @brief Creates a new execution context and links it to the previous context.
 * 
 * This function allocates memory for a new execution context and initializes it.
 * The data for the new context is represented as an object whose prototype is the data object
 * of the previous context. This establishes a hierarchical relationship between the contexts,
 * allowing the new context to inherit properties from the previous one.
 * 
 * The new context is also linked to the provided previous context, forming a chain of contexts
 * that represents the execution stack.
 * 
 * @param process A pointer to the process structure managing the execution state.
 * @param previous A pointer to the previous execution context, which becomes the parent
 *  of the newly created context.
 * 
 * @return A pointer to the newly created execution context.
 */
context_t *create_context(process_t *process, context_t *previous);

/**
 * @brief Destroys an execution context and returns the previous context.
 * 
 * This function deallocates the memory associated with the given execution context. It also
 * decrements the reference count of the context's data, freeing it if no longer in use.
 * The data object of the destroyed context is no longer available, but the previous context
 * (from which the current context inherited) is returned, allowing the caller to restore it
 * as the active context.
 * 
 * @param context A pointer to the execution context to be destroyed.
 * 
 * @return A pointer to the previous execution context.
 */
context_t *destroy_context(context_t *context);
