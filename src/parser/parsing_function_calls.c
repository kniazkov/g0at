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
#include "resources/messages.h"

/**
 * @brief Rule for handling an identifier followed by parentheses (function call).
 * 
 * This function processes a token of type `TOKEN_IDENTIFIER` followed by a pair of parentheses 
 * (`TOKEN_BRACKET_PAIR`), representing a function call. 
 * 
 * If an invalid token is encountered inside the parentheses, a `compilation_error_t` is returned
 * with details about the unexpected token. If the rule is successfully applied,
 * the function returns `NULL`.
 * 
 * The function may return one of three outcomes:
 * - Successful application: The rule is applied, and the tokens are collapsed into a single
 *   token.
 * - No application: The rule is not applicable to the provided token sequence.
 *   This is not considered an error.
 * - Error: A syntax error is encountered (e.g., unexpected tokens inside parentheses). 
 * @param identifier The token representing the identifier (function name).
 * @param memory A pointer to the `parser_memory_t` structure, which manages memory allocation for
 *  tokens, syntax tree nodes, and errors.
 * @return A pointer to a `compilation_error_t` if an error occurs, or `NULL` if no error.
 */
compilation_error_t *identifier_and_parentheses(token_t *identifier, parser_memory_t *memory) {
    assert(identifier->type == TOKEN_IDENTIFIER);
    if (identifier->right && identifier->right->type == TOKEN_BRACKET_PAIR
            && identifier->right->text[0] == '(') {
        expression_t *func_object = create_variable_node(memory->graph, identifier->text,
            identifier->length);
        expression_t **args = (expression_t **)alloc_from_arena(memory->tokens,
            identifier->right->children.count * sizeof(expression_t*));
        size_t args_count = 0;
        token_t *token = identifier->right->children.first;
        while (token != NULL) {
            if (token->type == TOKEN_EXPRESSION) {
                args[args_count++] = (expression_t *)token->node;
            } else {
                compilation_error_t *error = create_error_from_token(memory->tokens, token,
                    get_messages()->expected_expression, token->text);
                return error;
            }
            token = token->right;
        }
        node_t *func_call = create_function_call_node(memory->graph, func_object, args, args_count);
        collapse_tokens_to_token(memory->tokens, identifier, identifier->right,
            TOKEN_EXPRESSION, func_call);
    }
    return NULL;
}
