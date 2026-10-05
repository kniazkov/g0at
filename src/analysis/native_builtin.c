/** @file native_builtin.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks declaration identities and rejects any program-wide abs replacement.
 */
#include "native_builtin.h"

#include "graph/replacement.h"
#include "graph/variable.h"

#include <wchar.h>

static bool abs_name(string_view_t name) {
    return name.length == 3 && !wmemcmp(name.data, L"abs", 3);
}

static bool writes_abs(const node_t *node) {
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
            if (variable->declarator == get_builtin_declarator() && abs_name(variable->name))
                return true;
        }
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (writes_abs(get_node_child(node, i)))
            return true;
    return false;
}

static bool stable(const node_t *node) {
    while (node->parent)
        node = node->parent;
    return !writes_abs(node);
}

bool resolve_native_abs(const node_t *expression) {
    const node_t *origin = expression;
    for (size_t depth = 0; expression && depth < 64; depth++) {
        expression = replacement_original(expression);
        if (expression->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
            expression = get_node_child(expression, 0);
        } else if (expression->vtbl->type == NODE_VARIABLE) {
            const variable_t *variable = (const variable_t *)expression;
            const declarator_t *binding = variable->declarator;
            if (binding == get_builtin_declarator())
                return abs_name(variable->name) && stable(origin);
            if (!binding || binding->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
                return false;
            expression = get_node_child(&binding->base, 0);
        } else {
            return false;
        }
    }
    return false;
}

bool native_abs_capture(const function_summary_t *summary, const function_capture_t *capture) {
    if (capture->access != FUNCTION_CAPTURE_READ || !capture->declarator)
        return false;
    if (capture->declarator == get_builtin_declarator())
        return abs_name(capture->name) && stable(summary->function);
    return capture->declarator->base.vtbl->type == NODE_CONSTANT_DECLARATOR
           && resolve_native_abs(get_node_child(&capture->declarator->base, 0));
}
