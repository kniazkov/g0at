/**
 * @file properties.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Structural proofs combined with immediate-execution facts.
 */
#include "properties.h"

#include "graph/node.h"

/** @brief Creating a closure does not execute its body. */
static bool evaluation_is_pure(const node_t *node) {
    return node->vtbl->type == NODE_FUNCTION_OBJECT || node_has_flag(node, NODE_FLAG_PURE);
}

/** @brief Unknown constructs keep no purity proof; writes remain conservative even when local. */
static bool has_no_intrinsic_effect(const node_t *node) {
    switch (node->vtbl->type) {
        case NODE_ROOT:
        case NODE_ARGUMENT_LIST:
        case NODE_ARGUMENT:
        case NODE_FUNCTION_BODY:
        case NODE_STATEMENT_LIST:
        case NODE_NULL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_STATIC_STRING:
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_VARIABLE:
        case NODE_EXPRESSION_PARENTHESIZED:
        case NODE_FUNCTION_OBJECT:
        case NODE_STATEMENT_EXPRESSION:
        case NODE_RETURN:
        case NODE_IF_ELSE:
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_MULTIPLICATION:
        case NODE_DIVISION:
        case NODE_MODULO:
        case NODE_POWER:
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_LOGICAL_NOT:
        case NODE_BOOLEAN_CONVERSION:
        case NODE_LOGICAL_AND:
        case NODE_LOGICAL_OR:
        case NODE_BITWISE_NOT:
        case NODE_BITWISE_AND:
        case NODE_BITWISE_OR:
        case NODE_BITWISE_XOR:
        case NODE_SHIFT_LEFT:
        case NODE_SHIFT_RIGHT:
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
            return true;
        case NODE_FUNCTION_CALL:
            /* The reachability pass resolves native calls without guessing from their names. */
            return node_has_flag(node, NODE_FLAG_PURE);
        default:
            return false;
    }
}

void classify_node_properties(node_t *node, analysis_collector_t *collector) {
    bool children_pure = true;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        classify_node_properties(child, collector);
        if (!node_has_flag(child, NODE_FLAG_UNREACHABLE) && !evaluation_is_pure(child))
            children_pure = false;
    }
    bool pure = children_pure && has_no_intrinsic_effect(node);
    node->flags &= ~NODE_FLAG_PURE;
    if (node_has_flag(node, NODE_FLAG_UNREACHABLE)) {
        node->flags &= ~NODE_FLAG_C_COMPATIBLE;
    } else {
        if (pure)
            node->flags |= NODE_FLAG_PURE;
        switch (node->vtbl->type) {
            case NODE_INTEGER:
            case NODE_REAL:
                node->flags |= NODE_FLAG_C_COMPATIBLE;
                break;
            case NODE_EXPRESSION_PARENTHESIZED:
            case NODE_UNARY_PLUS:
                if (node_has_flag(get_node_child(node, 0), NODE_FLAG_C_COMPATIBLE))
                    node->flags |= NODE_FLAG_C_COMPATIBLE;
                break;
            default:
                break;
        }
    }
    add_analysis_event(collector, ANALYSIS_NODE_FLAGS, node, NULL, NULL);
}
