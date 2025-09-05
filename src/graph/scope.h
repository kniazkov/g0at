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
#include "lib/avl_tree.h"

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
     * @brief Globally unique identifier of this scope.
     *
     * Assigned by `create_scope()` from a static counter.
     * The first created scope gets id = 1, then increases monotonically.
     */
    unsigned int id;

    /**
     * @brief Pointer to the parent scope.
     *
     * If `NULL`, this scope is the global (root) scope.
     */
    scope_t *parent;

    /**
     * @brief Symbol bindings stored in this scope.
     *
     * Implemented as an AVL tree allocated from the same arena as the scope.
     * Keys are identifier names, values are pointers to the corresponding AST nodes.
     */
    avl_tree_arena_t *bindings;
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

/**
 * @brief Adds (or updates) a symbol in the given scope.
 *
 * Inserts a binding into scope bindings using the symbol name as the key.
 * If a symbol with the same name already exists in this scope, its value is
 * updated and the previous node pointer is returned.
 *
 * @param scope Target scope.
 * @param name  Symbol name (wide string).
 * @param node  AST node where the symbol is declared.
 * @return The previous node pointer if the symbol existed; otherwise NULL.
 */
const node_t* add_symbol_to_scope(scope_t *scope, const wchar_t *name, const node_t *node);

/**
 * @brief Looks up a symbol in the given scope only.
 *
 * @param scope Scope to search.
 * @param name  Symbol name.
 * @return The node pointer if found; otherwise NULL.
 */
const node_t* find_symbol_in_scope(const scope_t *scope, const wchar_t *name);

/**
 * @brief Looks up a symbol in the scope and its parents (inner-to-outer search).
 *
 * Walks the parent chain starting from scope. Returns on the first match.
 *
 * @param scope Starting scope.
 * @param name  Symbol name.
 * @return The node pointer if found; otherwise NULL.
 */
const node_t* find_symbol_in_scope_and_parents(const scope_t *scope, const wchar_t *name);
