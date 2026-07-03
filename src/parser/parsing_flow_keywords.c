/**
 * @file parsing_flow_keywords.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for control-flow keyword tokens.
 * 
 * This module parses keyword-driven control-flow constructs and collapses the
 * corresponding token ranges into statement AST nodes.
 */

#include <assert.h>

#include "parser.h"
#include "lib/arena.h"
#include "resources/messages.h"
#include "graph/statement.h"

/**
 * @brief Parses an `if` statement with an optional `else` branch.
 *
 * This function expects an `if` keyword followed by a parenthesized condition
 * expression and a statement or expression used as the true branch. If an
 * `else` keyword follows the true branch, the function also parses the false
 * branch.
 *
 * Expression branches are wrapped into statement-expression nodes. On success,
 * the consumed token range is collapsed into a single `TOKEN_STATEMENT`
 * containing a `NODE_IF_ELSE` AST node.
 *
 * @param token The `if` keyword token that starts the construct.
 * @param memory Parser memory used for AST allocation and error creation.
 * @param groups Token groups collected by the parser stage.
 * @return `NULL` on success, or a compilation error if the construct is invalid.
 */
static compilation_error_t *parsing_if_else(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    node_t *result;
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
        true_branch = (statement_t*)next->node;
    } else {
        true_branch = create_statement_expression_node(memory->graph, (expression_t*)next->node);
    }

    if (!next->right || next->right->type != TOKEN_ELSE) {
        // no else branch
        result = create_if_else_node(memory->graph, condition, true_branch, NULL);
        collapse_tokens_to_token(memory, token, next, TOKEN_STATEMENT, result);
        return false;
    }

    token_t *kw_else = next->right;
    next = kw_else->right;
    if (
        next == NULL ||
        (next->type != TOKEN_STATEMENT && next->type != TOKEN_EXPRESSION)
    ) {
        return create_error_from_token(
            memory->errors,
            kw_else,
            CRITICAL,
            get_messages()->expected_statement_after_else
        );
    }

    statement_t *false_branch;
    if (next->type == TOKEN_STATEMENT) {
        false_branch = (statement_t*)next->node;
    } else {
        false_branch = create_statement_expression_node(memory->graph, (expression_t*)next->node);
    }

    if (next->right && next->right->type == TOKEN_ELSE) {
        return create_error_from_token(
            memory->errors,
            next->right,
            CRITICAL,
            get_messages()->duplicate_else_branch
        );
    }

    result = create_if_else_node(memory->graph, condition, true_branch, false_branch);
    collapse_tokens_to_token(memory, token, next, TOKEN_STATEMENT, result);
    return false;
}

/**
 * @brief Parses a control-flow keyword token.
 *
 * Dispatches parsing to the concrete control-flow parser according to the token
 * type. 
 *
 * @param token The control-flow keyword token to parse.
 * @param memory Parser memory used for AST allocation and error creation.
 * @param groups Token groups collected by the parser stage.
 * @return `NULL` on success, or a compilation error if parsing fails.
 */
compilation_error_t *parsing_flow_keywords(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    switch(token->type) {
        case TOKEN_IF:
            return parsing_if_else(token, memory, groups);
        // add other parsers
        default:
            assert(false);
    }
    return NULL;
}

/**
 * @brief Reports an `else` keyword without a matching `if`.
 * 
 * The keyword `else` should have been consumed by the previous `if` parser. If it reaches this
 * parser, it is a standalone `else` without a matching `if`, so a critical compilation error
 * is created.
 *
 * @param token The standalone `else` keyword token.
 * @param memory Parser memory used for error creation.
 * @param groups Token groups collected by the parser stage.
 * @return A compilation error describing the unmatched `else`.
 */
compilation_error_t *parsing_else_keywords(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    return create_error_from_token(
        memory->errors,
        token,
        CRITICAL,
        get_messages()->else_without_if
    );
}
