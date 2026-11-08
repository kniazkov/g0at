/** @file operator.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Receiver-domain checks shared by purity, effects and native proofs.
 */
#include "operator.h"

#include "graph/expression.h"
#include "graph/replacement.h"
#include "graph/switch.h"

bool node_has_operator_receiver(const node_t *node) {
    if (node->vtbl->type == NODE_SWITCH) {
        for (size_t i = 0; i < switch_branch_count(node); i++) {
            if (switch_branch(node, i)->count)
                return true;
        }
        return false;
    }
    switch (node->vtbl->type) {
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_MULTIPLICATION:
        case NODE_DIVISION:
        case NODE_MODULO:
        case NODE_POWER:
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_PREFIX_INCREMENT:
        case NODE_PREFIX_DECREMENT:
        case NODE_POSTFIX_INCREMENT:
        case NODE_POSTFIX_DECREMENT:
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
        case NODE_LOGICAL_NOT:
        case NODE_LOGICAL_AND:
        case NODE_LOGICAL_OR:
        case NODE_BITWISE_NOT:
        case NODE_BITWISE_AND:
        case NODE_BITWISE_OR:
        case NODE_BITWISE_XOR:
        case NODE_SHIFT_LEFT:
        case NODE_SHIFT_RIGHT:
            return true;
        default:
            return false;
    }
}

bool node_may_dispatch_operator(const node_t *node, const c_expression_context_t *context) {
    if (!node_has_operator_receiver(node))
        return false;
    const node_t *receiver = get_node_child(node, 0);
    if (context) {
        for (const c_expression_proof_t *proof = context->head; proof; proof = proof->next) {
            if (proof->node == receiver)
                return proof->may_dispatch_operator;
        }
    } else if (is_expression(receiver->vtbl->type)
               && ((const expression_t *)receiver)->immediate_value) {
        return lattice_may_dispatch_operator(((const expression_t *)receiver)->immediate_value);
    }
    receiver = replacement_result(receiver);
    while (receiver->vtbl->type == NODE_EXPRESSION_PARENTHESIZED)
        receiver = replacement_result(get_node_child(receiver, 0));
    switch (receiver->vtbl->type) {
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_CHARACTER:
        case NODE_STATIC_STRING:
        case NODE_NULL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_FUNCTION_OBJECT:
            return false;
        default:
            return true;
    }
}
