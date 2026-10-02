/**
 * @file parsing_returns.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for return statements.
 */

#include "graph/expression.h"
#include "lib/arena.h"
#include "parser.h"

#include <assert.h>

/**
 * @brief Parses a return statement and creates a corresponding AST node.
 * @return NULL.
 */
compilation_error_t *
parsing_returns(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    assert(token->type == TOKEN_RETURN);
    if (token->right && token->right->type == TOKEN_EXPRESSION) {
        collapse_tokens_to_token(
            memory,
            token,
            token->right,
            TOKEN_STATEMENT,
            create_return_node(memory->graph, (expression_t *)token->right->node));
    } else {
        collapse_tokens_to_token(memory,
                                 token,
                                 token,
                                 TOKEN_STATEMENT,
                                 create_return_node(memory->graph, NULL));
    }
    return NULL;
}
