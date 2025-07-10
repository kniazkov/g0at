/**
 * @file token.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of token-related functions and operations.
 */

#include "token.h"
#include "graph/node.h"

string_value_t token_to_string(const token_t *token) {
    if (token->node) {
        return token->node->vtbl->generate_goat_code(token->node);
    }
    return STRING_VIEW_TO_VALUE(token->text);
}
