/**
 * @file parsing_returns.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for return statements.
 * 
 * This file contains the implementation of parsing and reduction logic specific to `return`
 * statements. It handles the transformation of `return` tokens and their associated expressions
 * into abstract syntax tree (AST) nodes representing return statements within functions.
 */

#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/arena.h"

/**
 * @brief Parses a return statement and creates a corresponding AST node.
 * 
 * This function processes a `TOKEN_RETURN` token, optionally followed by an expression token,
 * and collapses the sequence into a single `TOKEN_EXPRESSION` token representing a return
 * statement in the abstract syntax tree (AST).
 * 
 * If the return statement includes an expression, it is attached to the created return node.
 * Otherwise, a bare `return;` node is created.
 * 
 * @param token The `TOKEN_RETURN` token to process.
 * @param memory Parser memory context for allocations.
 * @param groups Token classification groups (unused in this function).
 * @return This function does not return errors.
 */
compilation_error_t *parsing_returns(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_RETURN);
    if (token->right && token->right->type == TOKEN_EXPRESSION) {
        collapse_tokens_to_token(
            memory->tokens, 
            token, 
            token->right,
            TOKEN_EXPRESSION, 
            create_return_node(memory->graph, (expression_t*)token->right->node)
        );
    } else {
        collapse_tokens_to_token(
            memory->tokens, 
            token, 
            token,
            TOKEN_EXPRESSION, 
            create_return_node(memory->graph, NULL)
        );
    }
    return NULL;
}
