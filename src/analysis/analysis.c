/**
 * @file analysis.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of static code analysis functions.
 *
 * This file provides the core implementation of the static analysis subsystem.
 * It walks the abstract syntax tree (AST), constructs and propagates scope
 * information, assigns node and scope identifiers, builds symbol bindings, and
 * performs semantic checks. These steps ensure that the code is structurally
 * and semantically correct before further compilation stages such as
 * optimization and code generation.
 */

#include "analysis.h"
#include "common/compilation_error.h"
#include "graph/node.h"
#include "lib/arena.h"

/**
 * @brief Recursively assigns node identifiers and connects nodes to lexical scopes.
 *
 * This function performs a depth-first traversal of the abstract syntax tree (AST),
 * assigning sequential identifiers to nodes and creating new scopes where required.
 * The numbering and scoping rules are:
 *
 * - For a node of type NODE_FUNCTION_OBJECT:
 *   - The function node itself is assigned the next identifier in the current scope.
 *   - A new scope is created for its body.
 *   - Node identifiers inside this new scope start from 1.
 *
 * - For a node of type NODE_STATEMENT_LIST:
 *   - The statement list node itself is assigned the next identifier in the current scope.
 *   - A new scope is created for its children.
 *   - Node identifiers continue from the current sequence (no reset).
 *
 * - For all other node types:
 *   - The node is assigned the next identifier in the current scope.
 *   - Its children are analyzed within the same scope and numbering sequence.
 *
 * @param node      The current AST node to analyze.
 * @param arena     The memory arena used for allocating new scopes.
 * @param scope     The scope that this node belongs to.
 * @param next_id   Pointer to the counter of node identifiers in the current scope.
 *  Updated as nodes are assigned ids.
 *
 * @note Each scope has its own sequence of node ids starting from 1, except
 *  statement list scopes which continue the numbering of their parent.
 */
static void assign_node_indexes_and_scopes(node_t *node, arena_t *arena, scope_t *scope,
        unsigned int *next_id) {
    const size_t child_count = node->vtbl->get_child_count(node);
    for (size_t child_id = 0; child_id < child_count; child_id++) {
        node_t *child = node->vtbl->get_child(node, child_id);
        switch (child->vtbl->type) {
            case NODE_FUNCTION_OBJECT: {
                child->scope = scope;
                child->id = (*next_id)++;
                scope_t *inner_scope = create_scope(arena, scope);
                unsigned int inner_counter = 1;
                assign_node_indexes_and_scopes(child, arena, inner_scope, &inner_counter);
                break;
            }
            case NODE_STATEMENT_LIST: {
                child->scope = scope;
                child->id = (*next_id)++;
                scope_t *inner_scope = create_scope(arena, scope);
                assign_node_indexes_and_scopes(child, arena, inner_scope, next_id);
                break;
            }
            default: {
                child->scope = scope;
                child->id = (*next_id)++;
                assign_node_indexes_and_scopes(child, arena, scope, next_id);
                break;
            }
        }
    }
}

compilation_error_t *analyze(node_t *root_node, arena_t *arena) {
    scope_t *root_scope = create_scope(arena, NULL);
    unsigned int node_counter = 0;
    assign_node_indexes_and_scopes(root_node, arena, root_scope, &node_counter);
    return NULL;
}
