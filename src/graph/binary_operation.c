/**
 * @file binary_operation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of binary operation expression methods.
 */

#include "binary_operation.h"

size_t binop_get_child_count(const node_t *node) {
    return 2;
}

node_t *binop_get_child(const node_t *node, size_t index) {
    const binary_operation_t *expr = (const binary_operation_t *)node;
    if (index == 0) {
        return &expr->left_operand->base;
    }
    if (index == 1) {
        return &expr->right_operand->base;
    }
    return NULL;
}

const wchar_t *binop_get_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"left";
    }
    if (index == 1) {
        return L"right";
    }
    return NULL;
}

bool binop_replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    binary_operation_t *expr = (binary_operation_t *)node;
    if (!is_expression(new_child->vtbl->type))
        return false;
    if ((node_t *)expr->left_operand == old_child)
        expr->left_operand = (expression_t *)new_child;
    else if (expr->right_operand && (node_t *)expr->right_operand == old_child)
        expr->right_operand = (expression_t *)new_child;
    else
        return false;
    return true;
}
