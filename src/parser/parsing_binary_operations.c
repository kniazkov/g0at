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
#include <wchar.h>

#include "parser.h"
#include "graph/binary_operation.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Validates that both operands of a binary operator are valid expressions.
 * 
 * This function checks whether the tokens to the left and right of the given operator token
 * exist and are valid expression tokens. If either operand is missing or is not an expression,
 * a compilation error is generated.
 * 
 * @param operator The operator token whose operands are to be checked.
 * @param memory A pointer to the parser memory structure used for error reporting.
 * @return A pointer to a `compilation_error_t` if operands are invalid, or `NULL` if valid.
 */
static compilation_error_t * check_operands(token_t *operator, parser_memory_t *memory) {
    token_t *left_token = operator->left;
    if (left_token == NULL) {
        return create_error_from_token(
            memory->tokens,
            operator,
            get_messages()->expected_expression,
            operator->text
        );
    }
    if (left_token->type != TOKEN_EXPRESSION) {
        return create_error_from_token(
            memory->tokens,
            left_token,
            get_messages()->expected_expression,
            left_token->text
        );
    }

    token_t *right_token = operator->right;
    if (right_token == NULL) {
        return create_error_from_token(
            memory->tokens,
            operator,
            get_messages()->expected_expression,
            operator->text
        );
    }
    if (right_token->type != TOKEN_EXPRESSION) {
        return create_error_from_token(
            memory->tokens,
            right_token,
            get_messages()->expected_expression,
            right_token->text
        );
    }

    return NULL;
}

/**
 * @brief Rule for handling comparison operators.
 *
 * This reduction processes a token sequence with a comparison operator and two
 * expression operands. It validates that both sides are expressions and
 * reduces the sequence into a single expression node representing the
 * corresponding comparison.
 *
 * Supported operators (currently):
 * - `<`  — creates a less-than comparison node (`NODE_LESS`, opcode `LESS`).
 *
 * Behavior:
 * - Verifies operands are valid expressions via @ref check_operands
 * - Builds a comparison AST node (e.g., @ref create_less_node for `<`)
 * - Collapses the three-token span (left, operator, right) into a single
 *   `TOKEN_EXPRESSION` using @ref collapse_tokens_to_token
 *
 * @param operator The token representing the comparison operator (must be `TOKEN_OPERATOR`).
 * @param memory Parser memory context used for allocations and error reporting.
 * @param groups Token classification groups (unused in this rule).
 * @return `NULL` on success; a pointer to @ref compilation_error_t on invalid operands
 *         or unsupported operator.
 *
 * @note Additional operators such as `<=`, `>`, `>=`, `==`, `!=` can be added in
 *       the future by extending the operator dispatch inside this function.
 *
 * @pre `operator->type == TOKEN_OPERATOR`
 *
 * @see check_operands
 * @see create_less_node
 * @see collapse_tokens_to_token
 */
compilation_error_t *parsing_comparison_operators(token_t *operator, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR);

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation;
    if (0 == wcscmp(operator->text.data , L"<")) {
        operation = create_less_node(memory->graph, left_operand, right_operand);
    }
    collapse_tokens_to_token(memory->tokens, operator->left, operator->right, TOKEN_EXPRESSION,
        &operation->base);
    return NULL;
}

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
    collapse_tokens_to_token(memory->tokens, operator->left, operator->right, TOKEN_EXPRESSION,
        &operation->base);
    return NULL;
}

/**
 * @brief Rule for handling multiplicative operators (*, /, %).
 * 
 * This function processes a token sequence with a multiplicative operator (`*`, `/`, `%`)
 * and two expression operands. It reduces the sequence into a single expression node
 * representing the corresponding binary operation.
 * 
 * @param operator The token representing the operator.
 * @param memory A pointer to the `parser_memory_t` structure, which manages allocation
 *  for tokens and nodes.
 * @param groups Token classification groups that may be updated during the reduction.
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *parsing_multiplicative_operators(token_t *operator, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR &&
        (operator->text.data[0] == L'*' || operator->text.data[0] == L'/'||
         operator->text.data[0] == L'%'));

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
    collapse_tokens_to_token(memory->tokens, operator->left, operator->right, TOKEN_EXPRESSION,
        &operation->base);
    return NULL;
}

/**
 * @brief Rule for handling exponentiation operators (`**`).
 * 
 * This function processes a token sequence with a power operator (`**`)
 * and two expression operands. It reduces the sequence into a single expression node
 * representing the power (exponentiation) operation.
 * 
 * @param operator The token representing the power operator.
 * @param memory A pointer to the `parser_memory_t` structure, which manages allocation
 *  for tokens and nodes.
 * @param groups Token classification groups that may be updated during the reduction.
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *parsing_power_operators(token_t *operator, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(operator->type == TOKEN_OPERATOR && operator->text.data[0] == L'*');

    compilation_error_t *error = check_operands(operator, memory);
    if (error) {
        return error;
    }

    expression_t *left_operand = (expression_t *)operator->left->node;
    expression_t *right_operand = (expression_t *)operator->right->node;
    expression_t *operation = create_power_node(memory->graph, left_operand, right_operand);
    collapse_tokens_to_token(memory->tokens, operator->left, operator->right, TOKEN_EXPRESSION,
        &operation->base);
    return NULL;
}
