/**
 * @file scope.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declaration of the scope structure and its operations.
 */

#pragma once

#include "lib/avl_tree.h"
#include "lib/value.h"

typedef struct scope_t scope_t;
typedef struct node_t node_t;
typedef struct declarator_t declarator_t;

/**
 * @brief A lexical scope in the abstract syntax tree (AST).
 *
 * A scope defines a lexical environment for symbol bindings. Scopes are chained together via a
 * parent pointer, forming a hierarchy of nested lexical environments.
 */
struct scope_t {
    /** @brief Globally unique identifier of this scope. */
    unsigned int id;

    /**
     * @brief Pointer to the parent scope.
     *
     * If `NULL`, this scope is the global (root) scope.
     */
    scope_t *parent;

    /** @brief Symbol bindings stored in this scope. */
    avl_tree_arena_t *bindings;
};

/**
 * @brief Creates a new, empty scope.
 * `parent`: The parent scope (may be NULL for global scope).
 */
scope_t *create_scope(arena_t *arena, scope_t *parent);

/**
 * @brief Adds (or updates) a symbol in the given scope.
 * @return The previous node pointer if the symbol existed; otherwise NULL.
 */
declarator_t *add_symbol_to_scope(scope_t *scope, const wchar_t *name, const declarator_t *node);

/**
 * @brief Looks up a symbol in the given scope only.
 * @return The node pointer if found; otherwise NULL.
 */
declarator_t *find_symbol_in_scope(const scope_t *scope, const wchar_t *name);

/**
 * @brief Looks up a symbol in the scope and its parents (inner-to-outer search).
 * @return The node pointer if found; otherwise NULL.
 */
declarator_t *find_symbol_in_scope_and_parents(const scope_t *scope, const wchar_t *name);
