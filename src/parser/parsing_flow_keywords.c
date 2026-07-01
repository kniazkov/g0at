/**
 * @file parsing_flow_keywords.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for ...
 * 
 * ...
 */

#include <assert.h>

#include "parser.h"
#include "lib/arena.h"
#include "resources/messages.h"
#include "graph/statement.h"

static compilation_error_t *parsing_if_else(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    token_t *brackets = token->right;
    if (
            brackets == NULL ||
            brackets->type != TOKEN_BRACKET_PAIR ||
            brackets->text.data[0] != L'(' ||
            brackets->children.count != 1 ||
            brackets->children.first->type != TOKEN_EXPRESSION
    ) {
        return create_error_from_token(
            memory->errors,
            token,
            CRITICAL,
            get_messages()->expected_condition_after_if
        );
    }
    expression_t *condition = (expression_t*)brackets->children.first->node;
    token_t *next = brackets->right;
    if (
        next == NULL ||
        (next->type != TOKEN_STATEMENT && next->type != TOKEN_EXPRESSION)
    ) {
        return create_error_from_token(
            memory->errors,
            brackets,
            CRITICAL,
            get_messages()->expected_statement_after_if
        );
    }
    statement_t *true_branch;
    if (next->type == TOKEN_STATEMENT) {
        true_branch = (statement_t*)token->node;
    } else {
        true_branch = create_statement_expression_node(memory->graph, (expression_t*)next->node);
    }
    node_t *node = create_if_else_node(memory->graph, condition, true_branch, NULL);
    collapse_tokens_to_token(memory, token, next, TOKEN_STATEMENT, node);
    return false;
}

/**
 * @brief Parses ...
 */
compilation_error_t *parsing_flow_keywords(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    switch(token->type) {
        case TOKEN_IF:
            return parsing_if_else(token, memory, groups);
        case TOKEN_ELSE:
            return create_error_from_token(
                memory->errors,
                token,
                CRITICAL,
                get_messages()->else_without_if
            );
        default:
            assert(false);
    }
    return NULL;
}
