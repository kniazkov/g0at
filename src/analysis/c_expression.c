/** @file c_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Storage and virtual dispatch for isolated C expression proofs.
 */
#include "c_expression.h"

#include "graph/node.h"
#include "lib/arena.h"

c_value_type_t c_expression_type(const c_expression_context_t *context, const node_t *node) {
    for (const c_expression_proof_t *proof = context ? context->head : NULL; proof;
         proof = proof->next) {
        if (proof->node == node)
            return proof->type;
    }
    return C_VALUE_UNKNOWN;
}

void record_c_expression(c_expression_context_t *context,
                         const node_t *node,
                         const lattice_element_t *value) {
    if (!context)
        return;
    c_value_type_t type = C_VALUE_UNKNOWN;
    if (node->vtbl->can_generate_c_code && node->vtbl->can_generate_c_code(node, value, context)) {
        type = classify_c_value_type(value->type);
        if (value->type == LATTICE_TRUE || value->type == LATTICE_FALSE
            || value->type == LATTICE_BOOLEAN)
            type = C_VALUE_BOOL;
        if (type == C_VALUE_UNSUPPORTED)
            type = C_VALUE_UNKNOWN;
    }
    for (c_expression_proof_t *proof = context->head; proof; proof = proof->next) {
        if (proof->node == node) {
            if (proof->type != type)
                proof->type = C_VALUE_UNKNOWN;
            return;
        }
    }
    c_expression_proof_t *proof = alloc_zeroed_from_arena(context->arena, sizeof(*proof));
    proof->node = node;
    proof->type = type;
    if (context->tail)
        context->tail->next = proof;
    else
        context->head = proof;
    context->tail = proof;
}

bool c_numeric_operands(const node_t *node, const c_expression_context_t *context) {
    if (!context)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        c_value_type_t type = c_expression_type(context, get_node_child(node, i));
        if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
            return false;
    }
    return get_node_child_count(node) != 0;
}

const wchar_t *c_expression_type_name(c_value_type_t type) {
    return type == C_VALUE_INT64    ? L"int64"
           : type == C_VALUE_DOUBLE ? L"double"
           : type == C_VALUE_BOOL   ? L"bool"
                                    : L"unknown";
}
