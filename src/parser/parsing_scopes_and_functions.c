/**
 * @file parsing_scopes_and_functions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for scope blocks and function declarations.
 */
#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "resources/messages.h"

/** @brief Initializes a statement list expression node from a `{...}` block. */
static void init_statement_list(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *node = create_statement_list_node(memory->graph);
    node->position = token->position;
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    expr->type = TOKEN_EXPRESSION;
    expr->position = token->position;
    expr->text = token->text;
    expr->node = node;
    replace_token(token, expr);
    token->type = TOKEN_STATEMENT_LIST;
    token->node = node;
    remove_token_from_group(token);
    append_token_to_group(&groups->statement_lists, token);
}

/** @brief Initializes a function object node for a function without arguments. */
static void init_function_wo_args(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *func_obj = create_function_object_node(memory->graph, NULL, 0);
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    collapse_tokens_to_token(memory, token->left, token,
        TOKEN_EXPRESSION, func_obj);
    token->type = TOKEN_FUNCTION_BODY;
    token->node = func_obj;
    append_token_to_group(&groups->function_objects, token);
}

/**
 * @brief Initializes a function object node with arguments.
 * @return A pointer to a `compilation_error_t` if an error occurred, or `NULL` on success.
 */
static compilation_error_t *init_function_with_args(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    compilation_error_t *error = NULL;
    string_view_t *arg_list = (string_view_t *)ALLOC(
        sizeof(string_view_t) * token->left->children.count);
    size_t arg_count = 0;
    token_t *arg_token = token->left->children.first;
    while (arg_token) {
        if (arg_token->type != TOKEN_IDENTIFIER) {
            string_value_t text = token_to_string(arg_token);
            error = create_error_from_token(
                memory->errors,
                token,
                CRITICAL,
                get_messages()->invalid_function_argument,
                text.data
            );
            FREE_STRING(text);
            goto cleanup;
        }
        arg_list[arg_count++] = arg_token->text;
        token_t *next = arg_token->right;
        if (!next) {
            break;
        }
        if (next->type != TOKEN_COMMA) {
            error = create_error_from_token(
                memory->errors,
                token,
                CRITICAL,
                get_messages()->expected_comma_between_args
            );
            goto cleanup;
        }
        arg_token = next->right;
    }
    node_t *func_obj = create_function_object_node(memory->graph, arg_list, arg_count);
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    collapse_tokens_to_token(memory, token->left->left, token,
        TOKEN_EXPRESSION, func_obj);
    token->type = TOKEN_FUNCTION_BODY;
    token->node = func_obj;
    append_token_to_group(&groups->function_objects, token);
cleanup:
    FREE(arg_list);
    return error;
}

/**
 * @brief Handles initial processing of scope blocks (curly brace pairs).
 * `token`: The opening brace token (must be TOKEN_BRACKET_PAIR with '{').
 * @return NULL on success, error if invalid token provided.
 */
compilation_error_t *parsing_scopes_and_functions(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_BRACKET_PAIR && token->text.data[0] == L'{');
    if (token->left) {
        if (token->left->type == TOKEN_FUNC) {
            // func { ... } - function without arguments
            init_function_wo_args(token, memory, groups);
            return NULL;
        }
        if (token->left->type == TOKEN_BRACKET_PAIR && token->left->text.data[0] == L'('
                && token->left->left && token->left->left->type == TOKEN_FUNC) {
            // func ( ... ) { ... } - function with arguments
            return init_function_with_args(token, memory, groups);
        }
    }
    init_statement_list(token, memory, groups);
    return NULL;
}

/**
 * @brief Processes statements within a statement list.
 * `token`: The scope expression token (must be TOKEN_EXPRESSION containing
 * NODE_STATEMENT_LIST).
 * @return NULL on success, compilation error if statement processing fails.
 */
compilation_error_t *parsing_statement_list_bodies(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_STATEMENT_LIST && token->node &&
        token->node->vtbl->type == NODE_STATEMENT_LIST);
    statement_list_processing_result_t result = process_statement_list(memory, &token->children);
    if (result.error) {
        return result.error;
    }
    fill_statement_list_node(token->node, result.list);
    return NULL;
}

/**
 * @brief Processes the body of a function object.
 * `token`: A token representing the function body (must have type `TOKEN_FUNCTION_BODY`).
 * @return `NULL` if processing succeeds, or a pointer to a `compilation_error_t` if an error
 * occurred during statement processing.
 */
compilation_error_t *parsing_function_bodies(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_FUNCTION_BODY && token->node
        && token->node->vtbl->type == NODE_FUNCTION_OBJECT);
    statement_list_processing_result_t result = process_statement_list(memory, &token->children);
    if (result.error) {
        return result.error;
    }
    fill_function_body(token->node, result.list);
    return NULL;
}
