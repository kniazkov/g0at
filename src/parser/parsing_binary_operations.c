/**
 * @file parsing_binary_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for handling binary operations.
 */

#include "graph/binary_operation.h"
#include "graph/logic.h"
#include "lib/arena.h"
#include "parser.h"
#include "resources/messages.h"

#include <assert.h>
#include <wchar.h>

/**
 * @brief Validates that both operands of a binary operator are valid expressions.
 * @return A pointer to a `compilation_error_t` if operands are invalid, or `NULL` if valid.
 */
static compilation_error_t *check_operands(token_t *operator, parser_memory_t * memory) {
    token_t *left_token = operator->left;
    if (left_token == NULL) {
        return create_error_from_token(memory->errors,
                                       operator,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       operator->text.data);
    }
    if (left_token->type != TOKEN_EXPRESSION) {
        return create_error_from_token(memory->errors,
                                       left_token,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       left_token->text.data);
    }

    token_t *right_token = operator->right;
    if (right_token == NULL) {
        return create_error_from_token(memory->errors,
                                       operator,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       operator->text.data);
    }
    if (right_token->type != TOKEN_EXPRESSION) {
        return create_error_from_token(memory->errors,
                                       right_token,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       right_token->text.data);
    }

    return NULL;
}

/**
 * @brief Rule for handling comparison operators.
 * `operator`: The token representing the comparison operator (must be `TOKEN_OPERATOR`).
 * @return `NULL` on success; a pointer to @ref compilation_error_t on invalid operands or
 * unsupported operator.
 * @pre `operator->type == TOKEN_OPERATOR`
 */
compilation_error_t *
parsing_comparison_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR);

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation = NULL;
    if (!wcscmp(operator->text.data, L"<"))
        operation = create_less_node(memory->graph, left_operand, right_operand);
    else if (!wcscmp(operator->text.data, L"<="))
        operation = create_less_or_equal_node(memory->graph, left_operand, right_operand);
    else if (!wcscmp(operator->text.data, L">"))
        operation = create_greater_node(memory->graph, left_operand, right_operand);
    else if (!wcscmp(operator->text.data, L">="))
        operation = create_greater_or_equal_node(memory->graph, left_operand, right_operand);
    else if (!wcscmp(operator->text.data, L"=="))
        operation = create_equal_node(memory->graph, left_operand, right_operand);
    else if (!wcscmp(operator->text.data, L"!="))
        operation = create_not_equal_node(memory->graph, left_operand, right_operand);

    assert(operation != NULL);
    collapse_tokens_to_token(memory,
                             operator->left,
                             operator->right,
                             TOKEN_EXPRESSION,
                             &operation->base);
    return NULL;
}

/**
 * @brief Rule for handling additive operators (plus and minus).
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *
parsing_additive_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR &&(operator->text.data[0] == L'+' || operator->text
                                                   .data[0] == L'-'));

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation;
    if (operator->text.data[0] == L'+') {
        operation = create_addition_node(memory->graph, left_operand, right_operand);
    } else {
        operation = create_subtraction_node(memory->graph, left_operand, right_operand);
    }
    collapse_tokens_to_token(memory,
                             operator->left,
                             operator->right,
                             TOKEN_EXPRESSION,
                             &operation->base);
    return NULL;
}

/**
 * @brief Rule for handling multiplicative operators (*, /, %).
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *parsing_multiplicative_operators(token_t *operator,
                                                      parser_memory_t * memory,
                                                      token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR &&(operator->text.data[0] == L'*' || operator->text
                                                   .data[0] == L'/' || operator->text
                                                   .data[0] == L'%'));

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation = NULL;
    if (operator->text.data[0] == L'*') {
        operation = create_multiplication_node(memory->graph, left_operand, right_operand);
    } else if (operator->text.data[0] == L'/') {
        operation = create_division_node(memory->graph, left_operand, right_operand);
    } else if (operator->text.data[0] == L'%') {
        operation = create_modulo_node(memory->graph, left_operand, right_operand);
    }
    collapse_tokens_to_token(memory,
                             operator->left,
                             operator->right,
                             TOKEN_EXPRESSION,
                             &operation->base);
    return NULL;
}

/**
 * @brief Rule for handling exponentiation operators (`**`).
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *
parsing_power_operators(token_t *operator, parser_memory_t * memory, token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR && operator->text.data[0] == L'*');

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation = create_power_node(memory->graph, left_operand, right_operand);
    collapse_tokens_to_token(memory,
                             operator->left,
                             operator->right,
                             TOKEN_EXPRESSION,
                             &operation->base);
    return NULL;
}

/** @brief Reduces logical and integer bitwise binary operators. */
compilation_error_t *
parsing_logic_operators(token_t *op, parser_memory_t *memory, token_groups_t *groups) {
    compilation_error_t *error = check_operands(op, memory);
    if (error)
        return error;
    expression_t *left = (expression_t *)op->left->node, *right = (expression_t *)op->right->node,
                 *expr = NULL;
    if (!wcscmp(op->text.data, L"&&"))
        expr = create_logical_and_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L"||"))
        expr = create_logical_or_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L"&"))
        expr = create_bitwise_and_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L"|"))
        expr = create_bitwise_or_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L"^"))
        expr = create_bitwise_xor_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L"<<"))
        expr = create_shift_left_node(memory->graph, left, right);
    if (!wcscmp(op->text.data, L">>"))
        expr = create_shift_right_node(memory->graph, left, right);
    assert(expr);
    collapse_tokens_to_token(memory, op->left, op->right, TOKEN_EXPRESSION, &expr->base);
    return NULL;
}
