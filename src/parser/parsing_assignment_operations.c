/**
 * @file parsing_assignment_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for handling assignment operations.
 */

#include "graph/assignment.h"
#include "lib/arena.h"
#include "parser.h"
#include "resources/messages.h"

#include <assert.h>

/**
 * @brief Rule for handling assignment operators.
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *
parsing_assignment_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR && operator->text.data[0] == L'=');

    token_t *left_token = operator->left;
    if (left_token == NULL) {
        compilation_error_t *error = create_error_from_token(memory->errors,
                                                             operator,
                                                             CRITICAL,
                                                             get_messages()->expected_lvalue,
                                                             operator->text.data);
        return error;
    }
    if (left_token->type != TOKEN_EXPRESSION || !left_token->node->vtbl->is_assignable_expression) {
        compilation_error_t *error = create_error_from_token(memory->errors,
                                                             left_token,
                                                             CRITICAL,
                                                             get_messages()->expected_lvalue,
                                                             operator->text.data);
        return error;
    }

    token_t *right_token = operator->right;
    if (right_token == NULL) {
        compilation_error_t *error = create_error_from_token(memory->errors,
                                                             operator,
                                                             CRITICAL,
                                                             get_messages()->expected_expression,
                                                             operator->text.data);
        return error;
    }
    if (right_token->type != TOKEN_EXPRESSION) {
        compilation_error_t *error = create_error_from_token(memory->errors,
                                                             right_token,
                                                             CRITICAL,
                                                             get_messages()->expected_expression,
                                                             operator->text.data);
        return error;
    }

    assignable_expression_t *left_operand = (assignable_expression_t *)left_token->node;
    expression_t *right_operand = (expression_t *)right_token->node;
    expression_t *operation = NULL;
    if (operator->text.data[0] == L'=') {
        operation = create_simple_assignment_node(memory->graph, left_operand, right_operand);
    }
    collapse_tokens_to_token(memory, left_token, right_token, TOKEN_EXPRESSION, &operation->base);
    return NULL;
}
