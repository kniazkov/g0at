/** @file parsing_if_else.c
 * @copyright 2026 Ivan Kniazkov
 * @brief If/else reduction and unmatched-else diagnostics.
 */
#include "graph/statement.h"
#include "parser.h"
#include "resources/messages.h"

/**
 * @brief Parses an `if` statement with an optional `else` branch.
 * @return `NULL` on success, or a compilation error if the construct is invalid.
 */
compilation_error_t *
parsing_if_else(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *result;
    token_t *brackets = token->right;
    if (brackets == NULL || brackets->type != TOKEN_BRACKET_PAIR || brackets->text.data[0] != L'('
        || brackets->children.count != 1 || brackets->children.first->type != TOKEN_EXPRESSION) {
        return create_error_from_token(memory->errors,
                                       token,
                                       CRITICAL,
                                       get_messages()->expected_condition_after_if);
    }

    expression_t *condition = (expression_t *)brackets->children.first->node;
    token_t *next = brackets->right;
    if (next == NULL || (next->type != TOKEN_STATEMENT && next->type != TOKEN_EXPRESSION)) {
        return create_error_from_token(memory->errors,
                                       brackets,
                                       CRITICAL,
                                       get_messages()->expected_statement_after_if);
    }

    statement_t *true_branch;
    if (next->type == TOKEN_STATEMENT) {
        true_branch = (statement_t *)next->node;
    } else {
        true_branch = create_statement_expression_node(memory->graph, (expression_t *)next->node);
    }

    if (next->right && next->right->type == TOKEN_SEMICOLON)
        next = next->right;
    if (!next->right || next->right->type != TOKEN_ELSE) {
        // no else branch
        result = create_if_else_node(memory->graph, condition, true_branch, NULL);
        collapse_tokens_to_token(memory, token, next, TOKEN_STATEMENT, result);
        return false;
    }

    token_t *kw_else = next->right;
    next = kw_else->right;
    if (next == NULL || (next->type != TOKEN_STATEMENT && next->type != TOKEN_EXPRESSION)) {
        return create_error_from_token(memory->errors,
                                       kw_else,
                                       CRITICAL,
                                       get_messages()->expected_statement_after_else);
    }

    statement_t *false_branch;
    if (next->type == TOKEN_STATEMENT) {
        false_branch = (statement_t *)next->node;
    } else {
        false_branch = create_statement_expression_node(memory->graph, (expression_t *)next->node);
    }

    result = create_if_else_node(memory->graph, condition, true_branch, false_branch);
    collapse_tokens_to_token(memory, token, next, TOKEN_STATEMENT, result);
    return false;
}

/** @brief Reports an `else` keyword without a matching `if`. */
compilation_error_t *
parsing_else_keywords(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    return create_error_from_token(memory->errors,
                                   token,
                                   CRITICAL,
                                   token->left && token->left->type == TOKEN_STATEMENT
                                           && token->left->node->vtbl->type == NODE_IF_ELSE
                                           && get_node_child_count(token->left->node) == 3
                                       ? get_messages()->duplicate_else_branch
                                       : get_messages()->else_without_if);
}
