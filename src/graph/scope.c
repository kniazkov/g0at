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

scope_t *create_scope(arena_t *arena, scope_t *parent) {
    static unsigned int last_id = 0;
    scope_t *scope = (scope_t*)alloc_zeroed_from_arena(arena, sizeof(scope_t));
    scope->id = ++last_id;
    scope->parent = parent;
    return scope;
}
