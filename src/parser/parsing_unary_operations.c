/** @file parsing_unary_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Prefix signs with power binding more tightly on their right.
 */
#include "parser.h"
#include "graph/unary_expression.h"
#include "resources/messages.h"

compilation_error_t *parsing_power_operators(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Reduces the right-associated power chain following an operand. */
static compilation_error_t *reduce_power_tail(token_t *operand, parser_memory_t *memory,
        token_groups_t *groups) {
    token_t *power = operand->right;
    if (!power || power->type != TOKEN_OPERATOR || power->text.length != 2 ||
            power->text.data[0] != L'*' || power->text.data[1] != L'*') return NULL;
    if (power->right && power->right->type == TOKEN_EXPRESSION) {
        compilation_error_t *error = reduce_power_tail(power->right, memory, groups);
        if (error) return error;
    }
    return parsing_power_operators(power, memory, groups);
}

compilation_error_t *parsing_unary_operators(token_t *sign, parser_memory_t *memory,
        token_groups_t *groups) {
    if (sign->left && sign->left->type == TOKEN_EXPRESSION) return NULL;
    token_t *operand = sign->right;
    if (!operand || operand->type != TOKEN_EXPRESSION)
        return create_error_from_token(memory->errors, sign, CRITICAL,
            get_messages()->expected_expression, sign->text);
    compilation_error_t *error = reduce_power_tail(operand, memory, groups);
    if (error) return error;
    expression_t *expr = (expression_t *)sign->right->node;
    expr = sign->text.data[0] == L'+' ? create_unary_plus_node(memory->graph, expr)
        : create_unary_minus_node(memory->graph, expr);
    collapse_tokens_to_token(memory, sign, sign->right, TOKEN_EXPRESSION, &expr->base);
    return NULL;
}
