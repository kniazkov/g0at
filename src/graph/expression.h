/**
 * @file expression.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the expression structure.
 * 
 * This file defines the `expression_t` structure, which represents an expression node in
 * the syntax tree of the language.
 */

#pragma once

#include "node.h"

/**
 * @brief Forward declaration of declarator structure (needed for variable & constant declaration).
 */
typedef struct declarator_t declarator_t;

/**
 * @struct expression_t
 * @brief The structure representing an expression node.
 */
struct expression_t {
    /**
     * @brief Base node structure, providing common attributes for all nodes.
     */
    node_t base;
};

/**
 * @brief Gets the singleton instance of the null expression node.
 * 
 * Returns the shared instance of the null expression node. This function
 * provides global access to the null node without requiring allocation.
 * 
 * @return Pointer to the singleton null node instance.
 */
node_t *get_null_node_instance();

/**
 * @brief Creates a new static string expression node.
 * 
 * This function initializes a static string expression node with the given string value
 * and its length. The string and the node are allocated in the specified memory arena.
 * 
 * @param arena A pointer to the memory arena for allocation.
 * @param data The string value for the static string expression.
 * @param length The length of the string (excluding null terminator).
 * @return A pointer to the newly created static string node, represented as a `node_t`.
 */
node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length);

/**
 * @brief Creates a new integer literal node.
 * 
 * Allocates and initializes a new integer literal node in the specified arena.
 * The node will contain the provided 64-bit integer value.
 * 
 * @param arena Memory arena for node allocation.
 * @param value The integer value to store in the node.
 * @return A pointer to the newly created integer node, represented as a `node_t`.
 */
node_t *create_integer_node(arena_t *arena, int64_t value);

/**
 * @brief Creates a new variable expression node.
 * 
 * This function allocates and initializes a new variable expression node in the syntax tree.
 * The node represents a variable identified by its name.
 * 
 * @param arena The memory arena for allocating the variable node.
 * @param name String representing the variable's name.
 * @return A pointer to the newly created variable expression node.
 */
expression_t *create_variable_node(arena_t *arena, string_view_t name);

/**
 * @brief Creates a declarator from an existing variable expression.
 * 
 * Constructs a new declarator structure to represent the declaration of
 * an existing variable. The resulting declarator can be used in declaration
 * nodes (like variable_declaration_t) to formally declare the variable.
 * 
 * @param expr Pointer to the variable expression to convert.
 * @return Pointer to newly allocated declarator_t with name copied from the variable and no
 *  initializer.
 * @warning The returned declarator is heap-allocated and must be freed by the caller
 *  when no longer needed.
 * @note The created declarator will have no initializer (NULL), which is valid for
 *  variable declarations but invalid for constant declarations.
 */
declarator_t *create_declarator_from_variable(const node_t *expr);

/**
 * @brief Creates a function call expression node with empty arguments.
 * 
 * This function initializes a function call node without arguments, which is useful when:
 * - The function call syntax is recognized early in parsing (high precedence)
 * - The arguments contain complex expressions that need to be parsed later
 * - The function object is known but arguments require additional processing
 * 
 * @param arena Memory arena for allocation.
 * @param func_object The callable expression (function object).
 * @return Pointer to the created function call node.
 * 
 * @note The arguments must be set later using set_function_call_arguments()
 * @see set_function_call_arguments
 */
node_t *create_function_call_node_without_args(arena_t *arena, expression_t *func_object);

/**
 * @brief Sets arguments for a previously created function call node.
 * 
 * This function completes the initialization of a function call node by setting its arguments.
 * 
 * @param node Function call node created with create_function_call_node_without_args().
 * @param arena Memory arena for argument array allocation.
 * @param args Array of argument expressions.abort
 * @param args_count Number of arguments.
 * 
 * @pre The node must be of function call type (`NODE_FUNCTION_CALL`)
 * @pre The node must not have arguments already set
 * 
 * @note This operation cannot be undone - arguments can only be set once.
 */
void set_function_call_arguments(node_t *node, arena_t *arena, 
        expression_t **args, size_t args_count);
