/**
 * @file node.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions for the basic node structure of the abstract syntax tree (AST).
 * 
 * This file contains the definition of the basic node structure for the abstract syntax tree (AST).
 * The AST is a tree representation of the syntactic structure of source code, where each node
 * represents a language construct (e.g., expression, statement, or declaration).
 */

#pragma once

#include "lib/value.h"

/**
 * @typedef node_t
 * @brief Forward declaration for the node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef node_t
 * @brief Forward declaration for the arena memory allocator.
 */
typedef struct arena_t arena_t;

/**
 * @enum node_type_t
 * @brief Enumeration of node types in the abstract syntax tree (AST).
 * 
 * Each node type represents a category of nodes in the AST, which correspond to different
 * language constructs (e.g., expressions, statements, literals). The node type is used to
 * uniquely identify the kind of information stored within the node and guide operations like
 * parsing, traversal, and code generation.
 * 
 * Types are used in the AST to determine the appropriate handling and processing for different
 * kinds of language constructs.
 */
typedef enum {
    /**
     * @brief Static string node type.
     * 
     * This node type represents a static string literal in the source code.
     * It stores the string content as a part of the AST.
     */
    NODE_STATIC_STRING = 0,

    /**
     * @brief Variable node type.
     * 
     * This node type represents a variable expression in the source code.
     * It stores the name of the variable, and can be used to represent variables
     * or other named entities (e.g., constants).
     */
    NODE_VARIABLE
} node_type_t;

/**
 * @struct node_vtbl_t
 * @brief The virtual table structure for nodes in the syntax tree.
 * 
 * This structure contains pointers to functions that implement the behavior of different types
 * of nodes in the syntax tree. The virtual table allows polymorphism, enabling different node
 * types to have their own implementations for specific operations.
 */
typedef struct {
    /**
     * @brief The type of the node.
     * 
     * This field specifies the type of the node. It is used to differentiate between different
     * node types in the syntax tree. The node type is critical for safe casting of the generic
     * `node_t` to the specific type of node it represents.
     */
    node_type_t type;

    /**
     * @brief Function pointer for converting a node to its string representation.
     * 
     * The `to_string` function converts the node into a string that closely resembles
     * the original source code representation of the entity. While the string may not 
     * exactly match the source code (due to optimizations or transformations made during
     * parsing and processing), it will represent the entity in a manner that is functionally
     * equivalent to the original code.
     * 
     * This function is typically used for debugging, logging, or pretty-printing the node 
     * in a format that is human-readable and indicative of the original source code.
     * 
     * @param node A pointer to the node that should be converted to a string.
     * @return The string representation of the node as a `string_value_t`.
     * 
     * @note The returned string is dynamically allocated, and the caller is responsible
     *  for freeing the memory when it is no longer needed.
     * @note Although the string representation may not match the original code exactly
     *  (due to possible optimizations), it will provide a syntactically equivalent or close
     *  approximation of the original entity.
     */
    string_value_t (*to_string)(const node_t *node);
} node_vtbl_t;

/**
 * @struct node_t
 * @brief The base structure for nodes in the syntax tree.
 * 
 * This structure represents a base node in the syntax tree of the Goat language. All nodes,
 * regardless of their specific type, share this common structure which contains a reference to
 * their virtual table. The virtual table allows polymorphic behavior for nodes of different types,
 * enabling specific behavior for each node type in the tree.
 * 
 * Nodes in the syntax tree represent various constructs in the parsed source code, and can
 * be extended with additional fields for specific node types as necessary.
 */
struct node_t {
    /**
     * @brief Pointer to the node's virtual table.
     * 
     * This field holds a pointer to the virtual table that contains function pointers specific
     * to the type of node. It allows polymorphic behavior and provides type-specific
     * implementations for operations on different node types in the tree.
     * 
     * @note The virtual table is typically used to safely cast `node_t` to a more 
     *  specific node type.
     */
    node_vtbl_t *vtbl;
};
