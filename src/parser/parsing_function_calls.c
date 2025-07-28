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
#include "lib/allocate.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Handles function call syntax (identifier followed by parentheses).
 * 
 * This rule matches the pattern: TOKEN_IDENTIFIER TOKEN_BRACKET_PAIR('(')
 * and converts it into a function call expression. The actual arguments
 * remain unparsed and are stored as TOKEN_FCALL_ARGS for later processing.
 *
 * Behavior:
 * - Creates variable node for function name
 * - Initializes function call node without arguments
 * - Marks bracket pair as containing unparsed arguments
 * - Collapses into TOKEN_EXPRESSION
 *
 * @param identifier The function name token (must be TOKEN_IDENTIFIER).
 * @param memory Parser memory context for allocations.abort
 * @param groups Token classification groups (adds to function_arguments group).
 * @return NULL on success, error if invalid syntax encountered.
 */
compilation_error_t *parsing_identifier_and_parentheses(token_t *identifier,
        parser_memory_t *memory, token_groups_t *groups) {
    assert(identifier->type == TOKEN_IDENTIFIER);
    if (identifier->right && identifier->right->type == TOKEN_BRACKET_PAIR
            && identifier->right->text.data[0] == '(') {
        expression_t *func_object = create_variable_node(memory->graph, identifier->text);
        node_t *func_call = create_function_call_node_without_args(memory->graph, func_object);
        token_t *args = identifier->right;
        args->type = TOKEN_FCALL_ARGS;
        args->node = func_call;
        collapse_tokens_to_token(memory->tokens, identifier, identifier->right,
            TOKEN_EXPRESSION, func_call);
        append_token_to_group(&groups->function_arguments, args);
    }
    return NULL;
}

/**
 * @brief Processes unparsed function call arguments from TOKEN_FCALL_ARGS.
 * 
 * Validates and converts raw argument tokens into proper expressions:
 * 1. Checks for valid expression sequence separated by commas
 * 2. Reports errors for:
 *    - Non-expression tokens where arguments expected
 *    - Missing comma between arguments
 *    - Trailing comma without following expression
 * 3. Finalizes function call node with parsed arguments
 *
 * @param container The TOKEN_FCALL_ARGS token containing argument tokens.
 * @param memory Parser memory context for allocations.
 * @param groups Token groups (unused in this rule).
 * @return NULL if arguments parsed successfully, error otherwise.
 */
compilation_error_t *parsing_function_call_args(token_t *container,
        parser_memory_t *memory, token_groups_t *groups) {
    assert(container->type == TOKEN_FCALL_ARGS);
    token_t *token = container->children.first;
    if (token == NULL) {
        return NULL;
    }
    expression_t **args = (expression_t **)ALLOC(container->children.count * sizeof(expression_t*));
    size_t args_count = 0;
    compilation_error_t *error = NULL;
    while (true) {
        if (token->type == TOKEN_EXPRESSION) {
            args[args_count++] = (expression_t *)token->node;
        } else {
            error = create_error_from_token(memory->tokens, token,
                get_messages()->expected_expression, token->text);
            goto cleanup;
        }
        token = token->right;
        if (token == NULL) {
            break;
        }
        if (token->type != TOKEN_COMMA) {
            error = create_error_from_token(memory->tokens, token,
                get_messages()->expected_comma_between_args);
            goto cleanup;
        }
        if (token->right == NULL) {
            error = create_error_from_token(memory->tokens, token->right ,
                get_messages()->expected_expr_after_comma);
            goto cleanup;
        }
        token = token->right;
    }
    set_function_call_arguments(container->node, memory->graph, args, args_count);
cleanup:
    FREE(args);
    return error;
}
