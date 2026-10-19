/** @file iteration.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Rejects provably scalar sources without relying on sampled call signatures.
 */
#include "iteration.h"

#include "abstract_state.h"
#include "graph/expression.h"
#include "graph/variable.h"
#include "resources/messages.h"

/** @brief Evaluates only closed expressions and immutable, non-dynamic bindings. */
static bool prepare(const node_t *node, abstract_state_t *state, arena_t *arena, size_t depth) {
    if (!node || depth > 64)
        return false;
    node_type_t type = node->vtbl->type;
    if (type == NODE_VARIABLE) {
        const variable_t *variable = (const variable_t *)node;
        const declarator_t *decl = variable->declarator;
        if (variable->dynamic_lookup || !decl || decl == get_builtin_declarator()
            || decl->base.vtbl->type != NODE_CONSTANT_DECLARATOR
            || (decl->base.parent && decl->base.parent->vtbl->type == NODE_FOR_IN))
            return false;
        node_t *initial = get_node_child(&decl->base, 0);
        if (!prepare(initial, state, arena, depth + 1))
            return false;
        const lattice_element_t *value =
            calculate_expression((expression_t *)initial, state, arena);
        set_in_abstract_state(state, decl, value);
        return state->control_flow == FLOW_NORMAL;
    }
    if (type == NODE_FUNCTION_OBJECT)
        return true;
    bool safe = (type >= NODE_NULL && type <= NODE_CHARACTER)
                || type == NODE_EXPRESSION_PARENTHESIZED || type == NODE_ADDITION
                || type == NODE_SUBTRACTION || (type >= NODE_LOGICAL_NOT && type <= NODE_NOT_EQUAL);
    if (!safe)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (!prepare(get_node_child(node, i), state, arena, depth + 1))
            return false;
    return true;
}

void validate_iteration_sources(node_t *root,
                                arena_t *arena,
                                arena_t *errors,
                                compilation_error_t **list) {
    if (root->vtbl->type == NODE_FOR_IN) {
        node_t *source = get_node_child(root, 0);
        abstract_state_t *state = create_abstract_state(arena);
        bool invalid = source->vtbl->type == NODE_FUNCTION_OBJECT;
        if (prepare(source, state, arena, 0)) {
            const lattice_element_t *value =
                calculate_expression((expression_t *)source, state, arena);
            invalid = state->control_flow == FLOW_NORMAL && value->type != LATTICE_TOP
                      && value->type != LATTICE_NOT_NULL && value->type != LATTICE_BOTTOM
                      && value->type != LATTICE_ARRAY && value->type != LATTICE_TYPED_ARRAY
                      && value->type != LATTICE_USER_DEFINED_OBJECT;
        }
        destroy_abstract_state(state);
        if (invalid) {
            compilation_error_t *error =
                create_error_from_node(errors,
                                       source,
                                       ERROR,
                                       get_messages()->invalid_iteration_source);
            error->next = *list;
            *list = error;
        }
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        validate_iteration_sources(get_node_child(root, i), arena, errors, list);
}
