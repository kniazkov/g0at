/**
 * @file parsing_parenthesized_expressions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for parenthesized expressions.
 */

#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "parser.h"
#include "resources/messages.h"

#include <assert.h>

/**
 * @brief Pre-processes a bracket pair into a parenthesized expression shell.
 *
 * No syntax validation is performed here beyond asserting the token kind; the actual check for
 * "exactly one inner expression" is deferred to the parsing step handled by @ref
 * parsing_parenthesized_expressions.
 * @return Always NULL (errors are reported in the parsing step).
 */
compilation_error_t *preparsing_parenthesized_expressions(token_t *token,
                                                          parser_memory_t *memory,
                                                          token_groups_t *groups) {
    assert(token->type == TOKEN_BRACKET_PAIR && token->text.data[0] == '(');
    if (token->left && token->left->type == TOKEN_IF) { // if (...
        remove_token_from_group(token);
        return NULL;
    }
    node_t *node = create_parenthesized_expression_node(memory->graph);
    token_t *expr = (token_t *)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    expr->type = TOKEN_EXPRESSION;
    expr->position = token->position;
    expr->text = token->text;
    expr->node = node;
    replace_token(token, expr);
    token->type = TOKEN_EXPRESSION_IN_BRACKETS;
    token->node = node;
    remove_token_from_group(token);
    append_token_to_group(&groups->preprocessed_parenthesized_expressions, token);
    return NULL;
}

/**
 * @brief Validates and finalizes a parenthesized expression container.
 *
 * Validation rules: - The container must have exactly one child token - That sole child must be
 * TOKEN_EXPRESSION
 * @return NULL on success; error object on invalid syntax.
 */
compilation_error_t *
parsing_parenthesized_expressions(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    assert(token->type == TOKEN_EXPRESSION_IN_BRACKETS);
    if (token->children.count != 1) {
        goto error;
    }
    node_t *node = token->node;
    token = token->children.first;
    if (token->type != TOKEN_EXPRESSION) {
        goto error;
    }
    fill_parenthesized_expression(node, (expression_t *)token->node);
    return NULL;
error:
    return create_error_from_token(memory->errors,
                                   token,
                                   CRITICAL,
                                   get_messages()->invalid_parenthesized_expression);
}
