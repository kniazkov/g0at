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

/**
 * @brief Parses a control-flow keyword token.
 * @return `NULL` on success, or a compilation error if parsing fails.
 */
compilation_error_t *
parsing_flow_keywords(token_t *token, parser_memory_t *memory, token_groups_t *groups) {
    switch (token->type) {
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
