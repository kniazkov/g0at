/** @file function_effects.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative syntactic effects; no call propagation or reachability claims.
 */
#include "function_effects.h"

#include "graph/expression.h"
#include "graph/variable.h"

#include <wchar.h>

const function_capture_t *find_function_capture(const function_summary_t *summary,
                                                const declarator_t *declarator,
                                                string_view_t name) {
    for (const function_capture_t *capture = summary->captures; capture; capture = capture->next) {
        if (capture->declarator == declarator && capture->name.length == name.length
            && !wmemcmp(capture->name.data, name.data, name.length))
            return capture;
    }
    return NULL;
}

/** @brief Parameters and block locals belong to their nearest enclosing function. */
static void
record_access(function_summary_t *summary, const node_t *node, uint32_t access, arena_t *arena) {
    if (node->vtbl->type != NODE_VARIABLE) {
        summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
        return;
    }
    const variable_t *variable = (const variable_t *)node;
    const declarator_t *decl = variable->declarator;
    if (!decl) {
        summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
        return;
    }
    const node_t *owner = &decl->base;
    while (owner && owner->vtbl->type != NODE_FUNCTION_OBJECT)
        owner = owner->parent;
    if (owner == summary->function)
        return;
    if (access & FUNCTION_CAPTURE_READ)
        summary->direct_effects |= FUNCTION_EFFECT_EXTERNAL_READ;
    if (access & FUNCTION_CAPTURE_WRITE)
        summary->direct_effects |= FUNCTION_EFFECT_EXTERNAL_WRITE;
    function_capture_t *capture =
        (function_capture_t *)find_function_capture(summary, decl, variable->name);
    if (!capture) {
        capture = alloc_zeroed_from_arena(arena, sizeof(*capture));
        capture->declarator = decl;
        capture->name = variable->name;
        function_capture_t **tail = &summary->captures;
        while (*tail)
            tail = &(*tail)->next;
        *tail = capture;
    }
    capture->access |= access;
}

/** @brief Assignment targets are writes; creating a closure does not execute its body. */
static void scan(function_summary_t *summary, const node_t *node, arena_t *arena) {
    switch (node->vtbl->type) {
        case NODE_FUNCTION_OBJECT:
            return;
        case NODE_VARIABLE:
            record_access(summary, node, FUNCTION_CAPTURE_READ, arena);
            return;
        case NODE_SIMPLE_ASSIGNMENT:
            scan(summary, get_node_child(node, 1), arena);
            record_access(summary, get_node_child(node, 0), FUNCTION_CAPTURE_WRITE, arena);
            return;
        case NODE_PREFIX_INCREMENT:
        case NODE_PREFIX_DECREMENT:
        case NODE_POSTFIX_INCREMENT:
        case NODE_POSTFIX_DECREMENT:
            record_access(summary,
                          get_node_child(node, 0),
                          FUNCTION_CAPTURE_READ | FUNCTION_CAPTURE_WRITE,
                          arena);
            return;
        case NODE_FUNCTION_CALL:
            summary->has_calls = true;
            break;
        case NODE_ROOT:
        case NODE_ARGUMENT_LIST:
        case NODE_FUNCTION_BODY:
        case NODE_ARGUMENT:
        case NODE_VARIABLE_DECLARATOR:
        case NODE_CONSTANT_DECLARATOR:
        case NODE_STATEMENT_LIST:
        case NODE_NULL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_STATIC_STRING:
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_EXPRESSION_PARENTHESIZED:
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_LOGICAL_NOT:
        case NODE_BOOLEAN_CONVERSION:
        case NODE_BITWISE_NOT:
        case NODE_LOGICAL_AND:
        case NODE_LOGICAL_OR:
        case NODE_BITWISE_AND:
        case NODE_BITWISE_OR:
        case NODE_BITWISE_XOR:
        case NODE_SHIFT_LEFT:
        case NODE_SHIFT_RIGHT:
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_MULTIPLICATION:
        case NODE_DIVISION:
        case NODE_MODULO:
        case NODE_POWER:
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
        case NODE_STATEMENT_EXPRESSION:
        case NODE_VARIABLE_DECLARATION:
        case NODE_CONSTANT_DECLARATION:
        case NODE_RETURN:
        case NODE_THROW:
        case NODE_TRY_CATCH:
        case NODE_IF_ELSE:
        case NODE_FOR:
        case NODE_FOR_IN:
        case NODE_WHILE:
        case NODE_DO_WHILE:
            break;
        default:
            summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
            break;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        scan(summary, get_node_child(node, i), arena);
}

void analyze_function_direct_effects(node_t *root) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        function_summary_set_t *set = get_function_summaries(root);
        for (function_summary_t *summary = set->head; summary; summary = summary->next) {
            summary->direct_effects = FUNCTION_EFFECT_NONE;
            summary->has_calls = false;
            summary->captures = NULL;
            scan(summary, get_node_child(root, 1), set->arena);
        }
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        analyze_function_direct_effects(get_node_child(root, i));
}
