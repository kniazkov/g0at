/**
 * @file parsing_binary_operations.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for handling binary operations.
 *
 * This file contains the implementation of reduction rules for binary operations such as
 * addition and subtraction. These rules are applied during the parsing process to create
 * new tokens and abstract syntax tree (AST) nodes representing binary operations.
 */

#include <assert.h>

#include "parser.h"
#include "graph/binary_operation.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Rule for handling additive operators (plus and minus).
 * 
 * This function processes a token sequence with an additive operator (`+` or `-`), followed by
 * expressions on both sides, and creates a binary operation node. It performs a reduction on
 * the sequence of tokens, turning it into a single token representing the binary operation
 * (addition or subtraction).
 * 
 * @param identifier The token representing the operator (either `+` or `-`).
 * @param memory A pointer to the `parser_memory_t` structure, which manages memory allocation
 *  for tokens, syntax tree nodes, and errors.
 * @param groups Token classification groups that may be updated during reduction.
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *parsing_additive_operators(token_t *operator, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR &&
        (operator->text.data[0] == L'+' || operator->text.data[0] == L'-'));
    
    token_t *left_token = operator->left;
    if (left_token == NULL) {
        compilation_error_t *error = create_error_from_token(memory->tokens, operator,
            get_messages()->expected_expression, operator->text);
        return error;
    }
    if (left_token->type != TOKEN_EXPRESSION) {
        compilation_error_t *error = create_error_from_token(memory->tokens, left_token,
            get_messages()->expected_expression, left_token->text);
        return error;
    }

    token_t *right_token = operator->right;
    if (right_token == NULL) {
        compilation_error_t *error = create_error_from_token(memory->tokens, operator,
            get_messages()->expected_expression, operator->text);
        return error;
    }
    if (right_token->type != TOKEN_EXPRESSION) {
        compilation_error_t *error = create_error_from_token(memory->tokens, right_token,
            get_messages()->expected_expression, right_token->text);
        return error;
    }
    
    expression_t *left_operand = (expression_t *)left_token->node;
    expression_t *right_operand = (expression_t *)right_token->node;
    expression_t *operation;
    if (operator->text.data[0] == L'+') {
        operation = create_addition_node(memory->graph, left_operand, right_operand);
    } else {
        operation = create_subtraction_node(memory->graph, left_operand, right_operand);
    }
    collapse_tokens_to_token(memory->tokens, left_token, right_token, TOKEN_EXPRESSION,
        &operation->base);
    return NULL;
}
