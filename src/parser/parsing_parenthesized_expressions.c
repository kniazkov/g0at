/**
 * @file parsing_parenthesized_expressions.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for parenthesized expressions.
 *
 * This file contains the implementation of two reduction rules that handle
 * expressions wrapped in parentheses. The first rule performs a light-weight
 * pre-processing step that creates an AST node for a parenthesized expression
 * and rewires tokens to preserve the original bracket pair as a container.
 * The second rule validates and finalizes the parenthesized expression by
 * ensuring there is exactly one inner expression, and then fills the AST node.
 */

#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Pre-processes a bracket pair into a parenthesized expression shell.
 *
 * This reduction transforms a TOKEN_BRACKET_PAIR('(') token into two tokens:
 *  - A new TOKEN_EXPRESSION token that takes the original position in the stream
 *    and is associated with a freshly created parenthesized-expression AST node.
 *  - The original token is repurposed to TOKEN_EXPRESSION_IN_BRACKETS and kept
 *    as a container for inner tokens (its children remain intact).
 *
 * No syntax validation is performed here beyond asserting the token kind;
 * the actual check for "exactly one inner expression" is deferred to the
 * parsing step handled by @ref parsing_parenthesized_expressions.
 *
 * @param token   A TOKEN_BRACKET_PAIR token representing '(' ... ')' with children.
 * @param memory  Parser memory context (tokens arena and AST arena).
 * @param groups  Token classification groups (the container is queued for a later pass).
 * @return Always NULL (errors are reported in the parsing step).
 */
compilation_error_t *preparsing_parenthesized_expressions(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_BRACKET_PAIR && token->text.data[0] == '(');
    node_t *node = create_parenthesized_expression_node(memory->graph); 
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    expr->type = TOKEN_EXPRESSION;
    expr->begin = token->begin;
    expr->end = token->end;
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
 * This reduction consumes a TOKEN_EXPRESSION_IN_BRACKETS container that came
 * from the pre-processing step and verifies that it contains exactly one
 * inner TOKEN_EXPRESSION. If valid, it fills the corresponding AST node
 * with that inner expression. Otherwise, it reports an error.
 *
 * Validation rules:
 * - The container must have exactly one child token
 * - That sole child must be TOKEN_EXPRESSION
 *
 * Errors:
 * - On violation of the rules above, returns an error created with
 *   messages.invalid_parenthesized_expression.
 *
 * @param token   The TOKEN_EXPRESSION_IN_BRACKETS container with children.
 * @param memory  Parser memory context (used for error allocation).
 * @param groups  Token groups (unused in this rule).
 * @return NULL on success; error object on invalid syntax.
 */
compilation_error_t *parsing_parenthesized_expressions(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_EXPRESSION_IN_BRACKETS);
    if (token->children.count != 1) {
        goto error;
    }
    node_t *node = token->node;
    token = token->children.first;
    if (token->type != TOKEN_EXPRESSION) {
        goto error;
    }
    fill_parenthesized_expression(node, (expression_t*)token->node);
    return NULL;
error:
    return create_error_from_token(
        memory->tokens,
        token,
        get_messages()->invalid_parenthesized_expression
    );
}
