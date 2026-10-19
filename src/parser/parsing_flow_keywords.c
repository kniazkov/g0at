/**
 * @file parsing_flow_keywords.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines reduction rules for control-flow keyword tokens.
 */

#include "parser.h"

#include <assert.h>

compilation_error_t *parsing_if_else(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *parsing_for(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *parsing_try_catch(token_t *, parser_memory_t *, token_groups_t *);

compilation_error_t *parsing_while(token_t *, parser_memory_t *, token_groups_t *);

/** @brief Identifies a statement that may need recursive reduction inside a do body. */
static bool flow_keyword(token_t *token) {
    return token
           && (token->type == TOKEN_IF || token->type == TOKEN_FOR || token->type == TOKEN_WHILE
               || token->type == TOKEN_DO || token->type == TOKEN_TRY);
}

/**
 * @brief Parses a control-flow keyword token.
 * @return `NULL` on success, or a compilation error if parsing fails.
 */
compilation_error_t *
parsing_flow_keywords(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    token_t *body = token->right;
    if (token->type == TOKEN_IF || token->type == TOKEN_FOR || token->type == TOKEN_WHILE)
        body = body ? body->right : NULL;
    if (flow_keyword(body)) {
        compilation_error_t *error = parsing_flow_keywords(body, memory, groups);
        if (error)
            return error;
    }
    if (token->type == TOKEN_IF && token->right) {
        body = token->right->right;
        token_t *tail = body ? body->right : NULL;
        if (tail && tail->type == TOKEN_SEMICOLON)
            tail = tail->right;
        if (tail && tail->type == TOKEN_ELSE && flow_keyword(tail->right)) {
            compilation_error_t *error = parsing_flow_keywords(tail->right, memory, groups);
            if (error)
                return error;
        }
    }
    switch (token->type) {
        case TOKEN_WHILE:
        case TOKEN_DO:
            return parsing_while(token, memory, groups);
        case TOKEN_TRY:
            return parsing_try_catch(token, memory, groups);
        case TOKEN_FOR:
            return parsing_for(token, memory, groups);
        case TOKEN_IF:
            return parsing_if_else(token, memory, groups);
        // add other parsers
        default:
            assert(false);
    }
    return NULL;
}
