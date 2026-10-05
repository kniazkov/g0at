/** @file unused_bindings.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Eliminates dead bindings by declaration identity, ignoring archived history.
 */
#include "unused_bindings.h"

#include "graph/replacement.h"
#include "graph/variable.h"
#include "simplification.h"

/** @brief Object-valued blocks may expose their own context; keep that storage. */
static bool exposed(const node_t *node) {
    for (const node_t *p = node->parent; p; p = p->parent) {
        if (p->vtbl->type == NODE_FUNCTION_BODY || p->vtbl->type == NODE_ROOT)
            return false;
        if (p->vtbl->type == NODE_STATEMENT_LIST)
            return !p->parent || p->parent->vtbl->type != NODE_STATEMENT_EXPRESSION;
    }
    return true;
}

/** @brief A simple write does not read mutable storage; updates and const writes do. */
static bool reads(const node_t *node, const declarator_t *binding) {
    node = replacement_result(node);
    if (is_deletion(node) || node_has_flag(node, NODE_FLAG_UNREACHABLE))
        return false;
    if (node->vtbl->type == NODE_VARIABLE)
        return ((const variable_t *)node)->declarator == binding;
    size_t first = 0;
    if (node->vtbl->type == NODE_SIMPLE_ASSIGNMENT) {
        const node_t *left = get_node_child(node, 0);
        if (left->vtbl->type == NODE_VARIABLE && ((const variable_t *)left)->declarator == binding
            && binding->base.vtbl->type == NODE_VARIABLE_DECLARATOR)
            first = 1;
    }
    for (size_t i = first; i < get_node_child_count(node); i++)
        if (reads(get_node_child(node, i), binding))
            return true;
    return false;
}

/** @brief Reading bound storage and creating a closure do not execute user code. */
static bool discardable(const node_t *node) {
    node = replacement_result(node);
    if (node->vtbl->type == NODE_EXPRESSION_PARENTHESIZED)
        return discardable(get_node_child(node, 0));
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        return true;
    if (node->vtbl->type == NODE_VARIABLE) {
        const declarator_t *binding = ((const variable_t *)node)->declarator;
        return binding && binding != get_builtin_declarator();
    }
    return can_discard_expression(node);
}

/** @brief Attaches a history wrapper without freeing shared descendants. */
static bool attach(node_t *parent, node_t *old, node_t *wrapper) {
    if (!replace_child_node(parent, old, wrapper))
        return false;
    wrapper->parent = parent;
    old->parent = wrapper;
    if (is_replacement(wrapper))
        get_node_child(wrapper, 1)->parent = wrapper;
    return true;
}

/** @brief Removes all simple stores, preserving their value in expression positions. */
static void remove_writes(node_t *node, const declarator_t *binding, arena_t *arena) {
    if (is_deletion(node))
        return;
    if (is_replacement(node)) {
        remove_writes(get_node_child(node, 1), binding, arena);
        return;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        const node_t *left =
            child->vtbl->type == NODE_SIMPLE_ASSIGNMENT ? get_node_child(child, 0) : NULL;
        if (left && left->vtbl->type == NODE_VARIABLE
            && ((const variable_t *)left)->declarator == binding) {
            expression_t *right = (expression_t *)get_node_child(child, 1);
            node_t *wrapper =
                node->vtbl->type == NODE_STATEMENT_EXPRESSION && discardable(&right->base)
                    ? (node_t *)create_expression_deletion(arena, (expression_t *)child)
                    : (node_t *)create_expression_replacement(arena, (expression_t *)child, right);
            attach(node, child, wrapper);
        } else {
            remove_writes(child, binding, arena);
        }
    }
}

static bool pass(node_t *node, node_t *root, arena_t *arena) {
    if (is_deletion(node))
        return false;
    if (is_replacement(node))
        return pass(get_node_child(node, 1), root, arena);
    bool changed = false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        node_type_t type = child->vtbl->type;
        if ((type == NODE_VARIABLE_DECLARATOR || type == NODE_CONSTANT_DECLARATOR)
            && !exposed(child) && !reads(root, (const declarator_t *)child)) {
            node_t *initial = get_node_child(child, 0);
            node_t *wrapper;
            if (!initial || discardable(initial)) {
                wrapper = (node_t *)create_statement_deletion(arena, child);
            } else {
                statement_t *effect =
                    create_statement_expression_node(arena, (expression_t *)initial);
                effect->base.scope = child->scope;
                initial->parent = &effect->base;
                wrapper =
                    (node_t *)create_statement_replacement(arena, (statement_t *)child, effect);
            }
            if (attach(node, child, wrapper)) {
                remove_writes(root, (const declarator_t *)child, arena);
                changed = true;
            }
        } else {
            changed |= pass(child, root, arena);
        }
    }
    return changed;
}

void eliminate_unused_bindings(node_t *root, arena_t *arena) {
    while (pass(root, root, arena)) {
        /* Every successful pass removes at least one executable declarator. */
    }
}
