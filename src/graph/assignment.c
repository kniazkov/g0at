/**
 * @file assignment.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of assignment operation methods.
 *
 * This file contains the implementation of virtual methods specific to assignment nodes.
 */

#include "assignment.h"

size_t assignment_get_child_count(const node_t *node) {
    return 2;
}

node_t* assignment_get_child(const node_t *node, size_t index) {
    const assignment_t *expr = (const assignment_t *)node;
    if (index == 0) {
        return &expr->left_operand->base.base;
    }
    if (index == 1) {
        return &expr->right_operand->base;
    }
    return NULL;
}

const wchar_t* assignment_get_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"target";
    }
    if (index == 1) {
        return L"value";
    }
    return NULL;
}
