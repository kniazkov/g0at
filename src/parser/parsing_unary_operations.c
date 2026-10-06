/** @file parsing_unary_operations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Prefix signs with power binding more tightly on their right.
 */
#include "graph/logic.h"
#include "graph/unary_expression.h"
#include "parser.h"
#include "resources/messages.h"

compilation_error_t *parsing_power_operators(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Reduces the right-associated power chain following an operand. */
static compilation_error_t *
reduce_power_tail(token_t *operand, parser_memory_t *memory, token_groups_t *groups) {
    token_t *power = operand->right;
    if (!power || power->type != TOKEN_OPERATOR || power->text.length != 2
        || power->text.data[0] != L'*' || power->text.data[1] != L'*')
        return NULL;
    if (power->right && power->right->type == TOKEN_EXPRESSION) {
        compilation_error_t *error = reduce_power_tail(power->right, memory, groups);
        if (error)
            return error;
    }
    return parsing_power_operators(power, memory, groups);
}

compilation_error_t *
parsing_unary_operators(token_t *sign, parser_memory_t *memory, token_groups_t *groups) {
    if (sign->left && sign->left->type == TOKEN_EXPRESSION) {
        if (sign->text.data[0] == L'+' || sign->text.data[0] == L'-')
            return NULL;
        return create_error_from_token(memory->errors,
                                       sign,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       sign->text.data);
    }
    token_t *operand = sign->right;
    if (operand && operand->type == TOKEN_INTEGER_MAGNITUDE) {
        token_t *tail = operand->right;
        if (sign->text.data[0] != L'-'
            || (tail && tail->type == TOKEN_OPERATOR && tail->text.length == 2
                && tail->text.data[0] == L'*' && tail->text.data[1] == L'*'))
            return create_error_from_token(memory->errors,
                                           operand,
                                           CRITICAL,
                                           get_messages()->integer_literal_out_of_range);
        collapse_tokens_to_token(memory,
                                 sign,
                                 operand,
                                 TOKEN_EXPRESSION,
                                 create_integer_node(memory->graph, INT64_MIN));
        return NULL;
    }
    if (!operand || operand->type != TOKEN_EXPRESSION)
        return create_error_from_token(memory->errors,
                                       sign,
                                       CRITICAL,
                                       get_messages()->expected_expression,
                                       sign->text.data);
    compilation_error_t *error = reduce_power_tail(operand, memory, groups);
    if (error)
        return error;
    expression_t *expr = (expression_t *)sign->right->node;
    switch (sign->text.data[0]) {
        case L'+':
            expr = create_unary_plus_node(memory->graph, expr);
            break;
        case L'-':
            expr = create_unary_minus_node(memory->graph, expr);
            break;
        case L'~':
            expr = create_bitwise_not_node(memory->graph, expr);
            break;
        default:
            expr = sign->text.length == 2 ? create_boolean_conversion_node(memory->graph, expr)
                                          : create_logical_not_node(memory->graph, expr);
            break;
    }
    collapse_tokens_to_token(memory, sign, sign->right, TOKEN_EXPRESSION, &expr->base);
    return NULL;
}
