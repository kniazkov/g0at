/** @file parsing_update_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Prefix and postfix updates, binding tighter than power.
 */
#include "graph/update_expression.h"
#include "parser.h"
#include "resources/messages.h"

/** @brief Unwraps a parenthesized variable before shells are finalized. */
static node_t *target_node(token_t *token, token_groups_t *groups) {
    if (!token || token->type != TOKEN_EXPRESSION)
        return NULL;
    node_t *node = token->node;
    while (node->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
        token_t *shell = groups->preprocessed_parenthesized_expressions.first;
        while (shell && shell->node != node)
            shell = shell->next_in_group;
        if (!shell || shell->children.count != 1 || shell->children.first->type != TOKEN_EXPRESSION)
            return NULL;
        node = shell->children.first->node;
    }
    return node->vtbl->type == NODE_VARIABLE ? node : NULL;
}

static compilation_error_t *
reduce_update(token_t *op, parser_memory_t *memory, token_groups_t *groups, bool postfix) {
    token_t *operand = postfix ? op->left : op->right;
    node_t *target = target_node(operand, groups);
    if (!target)
        return create_error_from_token(memory->errors,
                                       op,
                                       CRITICAL,
                                       get_messages()->expected_lvalue,
                                       op->text.data);
    bool decrement = op->text.data[0] == L'-';
    expression_t *expr =
        postfix
            ? (decrement
                   ? create_postfix_decrement_node(memory->graph, (assignable_expression_t *)target)
                   : create_postfix_increment_node(memory->graph,
                                                   (assignable_expression_t *)target))
            : (decrement
                   ? create_prefix_decrement_node(memory->graph, (assignable_expression_t *)target)
                   : create_prefix_increment_node(memory->graph,
                                                  (assignable_expression_t *)target));
    collapse_tokens_to_token(memory,
                             postfix ? operand : op,
                             postfix ? op : operand,
                             TOKEN_EXPRESSION,
                             &expr->base);
    return NULL;
}

compilation_error_t *
parsing_postfix_updates(token_t *op, parser_memory_t *memory, token_groups_t *groups) {
    if (!op->left || op->left->type != TOKEN_EXPRESSION)
        return NULL;
    return reduce_update(op, memory, groups, true);
}

compilation_error_t *
parsing_prefix_updates(token_t *op, parser_memory_t *memory, token_groups_t *groups) {
    return reduce_update(op, memory, groups, false);
}
