/**
 * @file parsing_function_calls.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for creating function call nodes.
 *
 * This file contains the implementation of reduction rules specific to function calls.
 * These rules are applied during the parsing process to create new tokens and 
 * abstract syntax tree (AST) nodes representing function calls. The rules define how
 * a sequence of tokens should be transformed into a valid function call expression.
 */

#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/arena.h"

/**
 * @brief Rule for handling an identifier followed by parentheses (function call).
 * 
 * This function handles the case where a token of type `TOKEN_IDENTIFIER` is followed
 * by a pair of parentheses (`TOKEN_BRACKET_PAIR`), indicating a function call.
 * 
 * @param identifier The token representing the identifier (function name).
 * @param tokens_memory The memory arena used for allocating new tokens.
 * @param graph_memory The memory arena used for allocating new AST nodes.
 */
void identifier_and_parentheses(token_t *identifier,
        arena_t *tokens_memory, arena_t *graph_memory) {
    assert(identifier->type == TOKEN_IDENTIFIER);
    if (identifier->right && identifier->right->type == TOKEN_BRACKET_PAIR
            && identifier->right->text[0] == '(') {
        expression_t *func_object = create_variable_node(graph_memory, identifier->text,
            identifier->length);
        expression_t **args = (expression_t **)alloc_from_arena(tokens_memory,
            identifier->right->children.count * sizeof(expression_t*));
        size_t args_count = 0;
        token_t *token = identifier->right->children.first;
        while (token != NULL) {
            if (token->type == TOKEN_EXPRESSION) {
                args[args_count++] = (expression_t *)token->node;
            } else {
                return;
            }
            token = token->right;
        }
        node_t *func_call = create_function_call_node(graph_memory, func_object, args, args_count);
        collapse_tokens_to_token(tokens_memory, identifier, identifier->right,
            TOKEN_EXPRESSION, func_call);
    }
}
