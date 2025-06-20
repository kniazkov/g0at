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
 * @param name A pointer to the wide-character string representing the variable's name.
 * @param name_length The length of the variable's name in characters (not including the null
 *  terminator).
 * @return A pointer to the newly created variable expression node.
 */
expression_t *create_variable_node(arena_t *arena, const wchar_t *name, size_t name_length);

/**
 * @brief Creates a function call expression node.
 * 
 * This function creates a new node representing a function call expression
 * in the AST.
 * 
 * @param arena The memory arena to allocate the node and arguments from.
 * @param func_object The function object being called.
 * @param args An array of arguments to be passed to the function.
 * @param args_count The number of arguments in the array.
 * @return A pointer to the created function call node.
 */
node_t *create_function_call_node(arena_t *arena, expression_t *func_object, expression_t **args,
        size_t args_count);
