/**
 * @file parsing_scopes_and_functions.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines reduction rules for scope blocks and function declarations.
 *
 * This file contains the implementation of reduction rules specific to processing
 * curly brace-delimited scope blocks and function declarations. These rules handle:
 * - Creation of scope nodes and function declarations from brace pairs
 * - Processing of statements within scope bodies
 */
#include <assert.h>

#include "parser.h"
#include "graph/expression.h"
#include "lib/arena.h"
#include "resources/messages.h"

static void init_scope(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *node = create_scope_node(memory->graph); 
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    expr->type = TOKEN_EXPRESSION;
    expr->begin = token->begin;
    expr->end = token->end;
    expr->text = token->text;
    expr->node = node;
    replace_token(token, expr);
    token->type = TOKEN_SCOPE_BODY;
    token->node = node;
    remove_token_from_group(token);
    append_token_to_group(&groups->scope_objects, token);
}

static void init_function_wo_args(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    node_t *func_obj = create_function_object_node(memory->graph, NULL, 0); 
    token_t *expr = (token_t*)alloc_zeroed_from_arena(memory->tokens, sizeof(token_t));
    collapse_tokens_to_token(memory->tokens, token->left, token,
        TOKEN_EXPRESSION, func_obj);
    token->type = TOKEN_FUNCTION_BODY;
    token->node = func_obj;
    append_token_to_group(&groups->function_objects, token);
}

/**
 * @brief Handles initial processing of scope blocks (curly brace pairs).
 * 
 * This rule matches TOKEN_BRACKET_PAIR('{') tokens and converts them into
 * scope expressions and function declarations.
 *
 * @param token The opening brace token (must be TOKEN_BRACKET_PAIR with '{').
 * @param memory Parser memory context for allocations.
 * @param groups Token classification groups.
 * @return NULL on success, error if invalid token provided.
 */
compilation_error_t *parsing_scopes_and_functions(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_BRACKET_PAIR && token->text.data[0] == L'{');
    if (token->left && token->left->type == TOKEN_FUNC) {
        // func { ... } - function without arguments
        init_function_wo_args(token, memory, groups);
        return NULL;
    }
    init_scope(token, memory, groups);
    return NULL;
}

/**
 * @brief Processes statements within a scope block.
 * 
 * Completes scope processing by:
 * 1. Parsing all child tokens as statements
 * 2. Filling the scope node with processed statements
 * 3. Maintaining proper AST relationships
 *
 * @param token The scope expression token (must be TOKEN_EXPRESSION containing NODE_SCOPE).
 * @param memory Parser memory context for allocations.
 * @param groups Token classification groups for statement processing.
 * @return NULL on success, compilation error if statement processing fails.
 * @pre Scope node must be created by parsing_scopes_and_functions()
 */
compilation_error_t *parsing_scope_bodies(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_SCOPE_BODY && token->node && token->node->vtbl->type == NODE_SCOPE);
    statement_list_processing_result_t result = process_statement_list(memory, &token->children);
    if (result.error) {
        return result.error;
    }
    fill_scope_node(token->node, memory->graph, result.list, result.count);
    return NULL;
}

compilation_error_t *parsing_function_bodies(token_t *token, parser_memory_t *memory,
        token_groups_t *groups) {
    assert(token->type == TOKEN_FUNCTION_BODY && token->node
        && token->node->vtbl->type == NODE_FUNCTION_OBJECT);
    statement_list_processing_result_t result = process_statement_list(memory, &token->children);
    if (result.error) {
        return result.error;
    }
    fill_function_body(token->node, memory->graph, result.list, result.count);
    return NULL;
}
