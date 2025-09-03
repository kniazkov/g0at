/**
 * @file scope.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Declaration of the scope structure and its operations.
 *
 * This header defines the interface for the scope abstraction, which represents
 * a lexical environment for variable bindings and name resolution in the
 * abstract syntax tree (AST). A scope provides a context in which identifiers
 * can be declared, looked up, and shadowed. Scopes are fundamental for
 * implementing block scoping, functions, and control structures in the Goat language.
 */

#pragma once

#include "lib/value.h"

/**
 * @typedef binding_t
 * @brief Forward declaration for the binding structure.
 */
typedef struct binding_t binding_t;

/**
 * @typedef scope_t
 * @brief Forward declaration for the scope structure.
 */
typedef struct scope_t scope_t;

/**
 * @typedef node_t
 * @brief Forward declaration for the node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef arena_t
 * @brief Forward declaration for the memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @struct binding_t
 * @brief Represents a symbol binding in a lexical scope.
 *
 * A binding describes a single declared entity (variable, constant) within a scope.
 * Bindings are organized as a singly linked list for each scope, which integrates well with arena
 * allocation (no need for dynamic resizing or deallocation).
 *
 * Each binding stores:
 * - Its name (as a string view into arena-managed memory)
 * - The AST node where it was declared
 * - A link to the next binding in the current scope
 */
struct binding_t {
    /**
     * @brief Pointer to the next binding in the current scope's linked list.
     *
     * Scopes maintain their bindings as a simple forward-linked list.
     * New declarations are typically pushed at the head.
     */
    binding_t *next;

    /**
     * @brief The name of the declared object.
     *
     * Stored as a string view into arena-allocated memory. The view does not
     * own the underlying memory and assumes that the string outlives the binding.
     */
    string_view_t name;

    /**
     * @brief Pointer to the AST node where this binding is declared.
     *
     * This allows semantic analysis, code generation, and error reporting
     * to reference the original declaration site.
     */
    node_t *node;
};

/**
 * @struct scope_t
 * @brief Represents a lexical scope in the abstract syntax tree (AST).
 *
 * A scope defines a lexical environment for symbol bindings. Each scope can
 * contain multiple bindings (variables, constants), which are stored in a singly linked list.
 * Scopes are chained together via a parent pointer, forming a hierarchy of nested lexical
 * environments.
 */
struct scope_t {
    /**
     * @brief Pointer to the parent scope.
     *
     * If `NULL`, this scope is the global (root) scope.
     */
    scope_t *parent;

    /**
     * @brief Pointer to the first binding in this scope.
     *
     * Bindings are organized as a singly linked list. If the scope has no
     * declarations, this pointer is NULL.
     */
    binding_t *bindings;
};

/**
 * @brief Creates a new, empty scope.
 *
 * Allocates a scope object in the given arena and links it to its parent scope.
 *
 * @param arena The memory arena from which to allocate the scope.
 * @param parent The parent scope (may be NULL for global scope).
 * @return Pointer to the newly created scope.
 */
scope_t *create_scope(arena_t *arena, scope_t *parent);