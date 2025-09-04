/**
 * @file binary_operation.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of binary operation expression methods.
 *
 * This file contains the implementation of virtual methods specific to binary operation nodes.
 */

 #include "binary_operation.h"

size_t binop_get_child_count(const node_t *node) {
    return 2;
}

node_t* binop_get_child(const node_t *node, size_t index) {
    const binary_operation_t *expr = (const binary_operation_t *)node;
    if (index == 0) {
        return &expr->left_operand->base;
    }
    if (index == 1) {
        return &expr->right_operand->base;
    }
    return NULL;
}

const wchar_t* binop_get_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"left";
    }
    if (index == 1) {
        return L"right";
    }
    return NULL;
}
