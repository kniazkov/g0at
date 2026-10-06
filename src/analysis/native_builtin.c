/** @file native_builtin.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks declaration identities and rejects any program-wide built-in replacement.
 */
#include "native_builtin.h"

#include "graph/replacement.h"
#include "graph/variable.h"

#include <wchar.h>

static native_builtin_kind_t name_kind(string_view_t name) {
    if (name.length == 3 && !wmemcmp(name.data, L"abs", 3))
        return NATIVE_BUILTIN_ABS;
    if (name.length == 4 && !wmemcmp(name.data, L"atan", 4))
        return NATIVE_BUILTIN_ATAN;
    return NATIVE_BUILTIN_NONE;
}

static bool writes_builtin(const node_t *node, native_builtin_kind_t kind) {
    node = replacement_original(node);
    node_type_t type = node->vtbl->type;
    if (type == NODE_SIMPLE_ASSIGNMENT || type == NODE_PREFIX_INCREMENT
        || type == NODE_PREFIX_DECREMENT || type == NODE_POSTFIX_INCREMENT
        || type == NODE_POSTFIX_DECREMENT) {
        const node_t *target = get_node_child(node, 0);
        while (target->vtbl->type == NODE_EXPRESSION_PARENTHESIZED)
            target = get_node_child(target, 0);
        if (target->vtbl->type == NODE_VARIABLE) {
            const variable_t *variable = (const variable_t *)target;
            if (variable->declarator == get_builtin_declarator()
                && name_kind(variable->name) == kind)
                return true;
        }
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (writes_builtin(get_node_child(node, i), kind))
            return true;
    return false;
}

static bool stable(const node_t *node, native_builtin_kind_t kind) {
    while (node->parent)
        node = node->parent;
    return !writes_builtin(node, kind);
}

native_builtin_kind_t resolve_native_builtin(const node_t *expression) {
    const node_t *origin = expression;
    for (size_t depth = 0; expression && depth < 64; depth++) {
        expression = replacement_original(expression);
        if (expression->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
            expression = get_node_child(expression, 0);
        } else if (expression->vtbl->type == NODE_VARIABLE) {
            const variable_t *variable = (const variable_t *)expression;
            const declarator_t *binding = variable->declarator;
            if (binding == get_builtin_declarator()) {
                native_builtin_kind_t kind = name_kind(variable->name);
                return kind != NATIVE_BUILTIN_NONE && stable(origin, kind) ? kind
                                                                           : NATIVE_BUILTIN_NONE;
            }
            if (!binding || binding->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
                return NATIVE_BUILTIN_NONE;
            expression = get_node_child(&binding->base, 0);
        } else {
            return NATIVE_BUILTIN_NONE;
        }
    }
    return NATIVE_BUILTIN_NONE;
}

native_builtin_kind_t capture_native_builtin(const function_summary_t *summary,
                                             const function_capture_t *capture) {
    if (capture->access != FUNCTION_CAPTURE_READ || !capture->declarator)
        return NATIVE_BUILTIN_NONE;
    if (capture->declarator == get_builtin_declarator()) {
        native_builtin_kind_t kind = name_kind(capture->name);
        return kind != NATIVE_BUILTIN_NONE && stable(summary->function, kind) ? kind
                                                                              : NATIVE_BUILTIN_NONE;
    }
    if (capture->declarator->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
        return NATIVE_BUILTIN_NONE;
    return resolve_native_builtin(get_node_child(&capture->declarator->base, 0));
}
