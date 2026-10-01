/**
 * @file parsing_function_calls.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for creating function call nodes.
 */

#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "resources/messages.h"

/**
 * @brief Handles function call syntax (identifier followed by parentheses).
 * `identifier`: The function name token (must be TOKEN_IDENTIFIER).
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
        collapse_tokens_to_token(memory, identifier, identifier->right,
            TOKEN_EXPRESSION, func_call);
        append_token_to_group(&groups->function_arguments, args);
    }
    return NULL;
}

/**
 * @brief Processes unparsed function call arguments from TOKEN_FCALL_ARGS.
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
            error = create_error_from_token(
                memory->errors,
                token,
                CRITICAL,
                get_messages()->expected_expression,
                token->text
            );
            goto cleanup;
        }
        token = token->right;
        if (token == NULL) {
            break;
        }
        if (token->type != TOKEN_COMMA) {
            error = create_error_from_token(
                memory->errors,
                token,
                CRITICAL,
                get_messages()->expected_comma_between_args
            );
            goto cleanup;
        }
        if (token->right == NULL) {
            error = create_error_from_token(
                memory->errors,
                token->right,
                CRITICAL,
                get_messages()->expected_expr_after_comma
            );
            goto cleanup;
        }
        token = token->right;
    }
    set_function_call_arguments(container->node, memory->graph, args, args_count);
cleanup:
    FREE(args);
    return error;
}
