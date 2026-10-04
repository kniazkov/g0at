/** @file c_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Storage and virtual dispatch for isolated C expression proofs.
 */
#include "c_expression.h"

#include "graph/node.h"
#include "lib/arena.h"

#include <math.h>

c_value_type_t c_expression_type(const c_expression_context_t *context, const node_t *node) {
    for (const c_expression_proof_t *proof = context ? context->head : NULL; proof;
         proof = proof->next) {
        if (proof->node == node)
            return proof->type;
    }
    return C_VALUE_UNKNOWN;
}

const lattice_element_t *c_expression_constant(const c_expression_context_t *context,
                                               const node_t *node) {
    for (const c_expression_proof_t *proof = context ? context->head : NULL; proof;
         proof = proof->next) {
        if (proof->node == node)
            return proof->type != C_VALUE_UNKNOWN && proof->discardable ? proof->constant : NULL;
    }
    return NULL;
}

bool c_constants_equal(const lattice_element_t *left, const lattice_element_t *right) {
    if (!left || !right || left->type != right->type)
        return false;
    if (left->type == LATTICE_INTEGER_CONSTANT)
        return ((const integer_constant_element_t *)left)->value
               == ((const integer_constant_element_t *)right)->value;
    if (left->type == LATTICE_REAL_CONSTANT) {
        double a = ((const real_constant_element_t *)left)->value;
        double b = ((const real_constant_element_t *)right)->value;
        return a == b && !!signbit(a) == !!signbit(b);
    }
    return left->type == LATTICE_TRUE || left->type == LATTICE_FALSE;
}

/** @brief Only total operations in the numeric C subset may lose their evaluation. */
static bool discardable(const node_t *node, const c_expression_context_t *context) {
    switch (node->vtbl->type) {
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_VARIABLE:
        case NODE_EXPRESSION_PARENTHESIZED:
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_MULTIPLICATION:
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
            break;
        default:
            return false;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        const node_t *child = get_node_child(node, i);
        const c_expression_proof_t *proof = context->head;
        while (proof && proof->node != child)
            proof = proof->next;
        if (!proof || proof->type == C_VALUE_UNKNOWN || !proof->discardable)
            return false;
    }
    return true;
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
    bool total = type != C_VALUE_UNKNOWN && discardable(node, context);
    const lattice_element_t *constant = total && c_constants_equal(value, value) ? value : NULL;
    for (c_expression_proof_t *proof = context->head; proof; proof = proof->next) {
        if (proof->node == node) {
            if (proof->type != type)
                proof->type = C_VALUE_UNKNOWN;
            proof->discardable &= total;
            if (!c_constants_equal(proof->constant, constant))
                proof->constant = NULL;
            return;
        }
    }
    c_expression_proof_t *proof = alloc_zeroed_from_arena(context->arena, sizeof(*proof));
    proof->node = node;
    proof->type = type;
    proof->discardable = total;
    proof->constant = constant;
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
