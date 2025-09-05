/**
 * @file scope.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the scope structure and its operations.
 *
 * This file provides the concrete implementation of the scope abstraction,
 * including memory allocation, symbol binding, lookup, and chaining of parent
 * scopes. Each scope forms a lexical environment where variables can be
 * declared without interfering with outer scopes. Together, nested scopes
 * form the basis for block scoping, function bodies, and other language
 * constructs that require isolated environments.
 */

#include "scope.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

scope_t *create_scope(arena_t *arena, scope_t *parent) {
    static unsigned int last_id = 0;
    scope_t *scope = (scope_t*)alloc_from_arena(arena, sizeof(scope_t));
    scope->id = ++last_id;
    scope->parent = parent;
    scope->bindings = create_avl_tree_arena(arena, string_comparator);
    return scope;
}

const node_t* add_symbol_to_scope(scope_t *scope, const wchar_t *name, const node_t *node) {
    return (const node_t*)set_in_avl_tree_arena(
        scope->bindings,
        (void*)name,
        (value_t){.ptr = (void*)node}
    ).ptr;
}

const node_t* find_symbol_in_scope(const scope_t *scope, const wchar_t *name) {
    return (const node_t*)get_from_avl_tree(
        &scope->bindings->base,
        (void*)name
    ).ptr;
}

const node_t* find_symbol_in_scope_and_parents(const scope_t *scope, const wchar_t *name) {
    const node_t* node = find_symbol_in_scope(scope, name);
    if (node) {
        return node;
    }
    if (scope->parent) {
        return find_symbol_in_scope_and_parents(scope->parent, name);
    }
    return NULL;
}
