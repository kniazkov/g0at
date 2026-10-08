/** @file native_builtin.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks declaration identities and rejects any program-wide built-in replacement.
 */
#include "native_builtin.h"

#include "builtins/registry.h"
#include "codegen/c_native.h"
#include "graph/replacement.h"
#include "graph/variable.h"

#include <wchar.h>

static bool writes_builtin(const node_t *node, const builtin_function_t *builtin) {
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
                && find_builtin_function(variable->name) == builtin)
                return true;
        }
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (writes_builtin(get_node_child(node, i), builtin))
            return true;
    return false;
}

static bool stable(const node_t *node, const builtin_function_t *builtin) {
    while (node->parent)
        node = node->parent;
    return !writes_builtin(node, builtin);
}

const builtin_function_t *resolve_native_builtin(const node_t *expression) {
    const node_t *origin = expression;
    for (size_t depth = 0; expression && depth < 64; depth++) {
        expression = replacement_original(expression);
        if (expression->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
            expression = get_node_child(expression, 0);
        } else if (expression->vtbl->type == NODE_VARIABLE) {
            const variable_t *variable = (const variable_t *)expression;
            const declarator_t *binding = variable->declarator;
            if (binding == get_builtin_declarator()) {
                const builtin_function_t *builtin = find_builtin_function(variable->name);
                return builtin && c_native_builtin_supported(builtin) && stable(origin, builtin)
                           ? builtin
                           : NULL;
            }
            if (!binding || binding->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
                return NULL;
            expression = get_node_child(&binding->base, 0);
        } else {
            return NULL;
        }
    }
    return NULL;
}

const builtin_function_t *capture_native_builtin(const function_summary_t *summary,
                                                 const function_capture_t *capture) {
    if (capture->access != FUNCTION_CAPTURE_READ || !capture->declarator)
        return NULL;
    if (capture->declarator == get_builtin_declarator()) {
        const builtin_function_t *builtin = find_builtin_function(capture->name);
        return builtin && c_native_builtin_supported(builtin) && stable(summary->function, builtin)
                   ? builtin
                   : NULL;
    }
    if (capture->declarator->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
        return NULL;
    return resolve_native_builtin(get_node_child(&capture->declarator->base, 0));
}
