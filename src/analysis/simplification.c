/** @file simplification.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative scalar folding and replacement attachment.
 */
#include "simplification.h"

#include "abstract_state.h"
#include "graph/expression.h"
#include "graph/replacement.h"
#include "lattice.h"

#include <assert.h>

/** @brief Scalar literals have no observable evaluation. */
static bool is_literal(const node_t *node) {
    switch (node->vtbl->type) {
        case NODE_NULL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_STATIC_STRING:
            return true;
        default:
            return false;
    }
}

/** @brief Calls may throw or diverge even when their normal return is constant. */
static bool has_only_scalar_operations(const node_t *node) {
    node = replacement_result(node);
    if (!is_expression(node->vtbl->type) || node->vtbl->type == NODE_FUNCTION_CALL
        || node->vtbl->type == NODE_FUNCTION_OBJECT || node->vtbl->type == NODE_STATEMENT_LIST
        || !node_has_flag(node, NODE_FLAG_PURE))
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!has_only_scalar_operations(get_node_child(node, i)))
            return false;
    }
    return true;
}

bool can_discard_expression(const node_t *node) {
    node = replacement_result(node);
    if (is_literal(node))
        return true;
    if (!has_only_scalar_operations(node))
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!can_discard_expression(get_node_child(node, i)))
            return false;
    }
    const lattice_element_t *value = ((const expression_t *)node)->immediate_value;
    /* A normal abstract result need not prove that every path avoids an exception. */
    return value
           && (value->type == LATTICE_NULL || value->type == LATTICE_TRUE
               || value->type == LATTICE_FALSE || value->type == LATTICE_INTEGER_CONSTANT
               || value->type == LATTICE_REAL_CONSTANT || value->type == LATTICE_STRING_CONSTANT);
}

/** @brief Builds scalar literals without formatting and reparsing their values. */
static node_t *literal_from_value(arena_t *arena, const lattice_element_t *value) {
    if (!value)
        return NULL;
    switch (value->type) {
        case LATTICE_NULL:
            return create_null_node(arena);
        case LATTICE_TRUE:
            return create_true_node(arena);
        case LATTICE_FALSE:
            return create_false_node(arena);
        case LATTICE_INTEGER_CONSTANT:
            return create_integer_node(arena, ((const integer_constant_element_t *)value)->value);
        case LATTICE_REAL_CONSTANT:
            return create_real_number_node(arena, ((const real_constant_element_t *)value)->value);
        case LATTICE_STRING_CONSTANT: {
            string_view_t text = ((const string_constant_element_t *)value)->value;
            return create_static_string_node(arena, text.data, text.length);
        }
        default:
            return NULL;
    }
}

node_t *simplify_constant_expression(node_t *node, arena_t *arena) {
    if (!has_only_scalar_operations(node))
        return node;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!can_discard_expression(get_node_child(node, i)))
            return node;
    }
    const lattice_element_t *value = ((expression_t *)node)->immediate_value;
    if (!value) {
        /* Closed literal expressions are also safe inside deferred function bodies. */
        size_t count = get_node_child_count(node);
        if (!count)
            return node;
        for (size_t i = 0; i < count; i++) {
            if (!is_literal(replacement_result(get_node_child(node, i))))
                return node;
        }
        abstract_state_t *state = create_abstract_state(arena);
        value = calculate_node(node, state, arena);
        if (state->control_flow != FLOW_NORMAL)
            value = NULL;
        destroy_abstract_state(state);
    }
    node_t *literal = literal_from_value(arena, value);
    if (!literal)
        return node;
    literal->scope = node->scope;
    literal->position = node->position;
    literal->flags = NODE_FLAG_PURE;
    if (can_generate_c_code_from_node(literal, value))
        literal->flags |= NODE_FLAG_C_COMPATIBLE;
    ((expression_t *)literal)->immediate_value = value;
    return literal;
}

void simplify_graph(node_t *node, arena_t *arena) {
    if (is_replacement(node) || node_has_flag(node, NODE_FLAG_UNREACHABLE))
        return;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        simplify_graph(child, arena);
        if (is_replacement(child) || node_has_flag(child, NODE_FLAG_UNREACHABLE))
            continue;
        node_t *result = child->vtbl->simplify(child, arena);
        if (result == child)
            continue;
        node_t *wrapper;
        if (is_expression(child->vtbl->type))
            wrapper = (node_t *)create_expression_replacement(arena,
                                                              (expression_t *)child,
                                                              (expression_t *)result);
        else
            wrapper = (node_t *)create_statement_replacement(arena,
                                                             (statement_t *)child,
                                                             (statement_t *)result);
        if (replace_child_node(node, child, wrapper)) {
            wrapper->parent = node;
            child->parent = wrapper;
            /* Historical edges are non-owning; parent follows the executable tree. */
            result->parent = wrapper;
        }
    }
}

void restore_graph(node_t *node) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        while (is_replacement(child) || is_deletion(child)) {
            node_t *original = get_node_child(child, 0);
            bool replaced = replace_child_node(node, child, original);
            assert(replaced);
            child = original;
        }
        child->parent = node;
        restore_graph(child);
    }
}
