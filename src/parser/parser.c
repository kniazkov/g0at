/**
 * @file parser.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements the functions for the parser.
 *
 * This file contains the implementation of functions for the parser, which transforms a token
 * stream into an abstract syntax tree (AST). The parser ensures that the token stream adheres
 * to the grammar of the language and builds a tree structure suitable for further analysis
 * and code generation.
 */

#include <stdbool.h>

#include "parser.h"
#include "compilation_error.h"
#include "lib/arena.h"
#include "scanner/scanner.h"
#include "resources/messages.h"

static compilation_error_t *scan_and_analyze_for_brackets(arena_t *arena, scanner_t *scan,
        token_list_t *list, const token_t *opening_token, const token_t **closing_token) {
    const token_t *previous = opening_token;
    while(true) {
        token_t *token = get_token(scan);
        if (token == NULL) {
            if (opening_token == NULL) {
                return NULL; // no opening bracket - no error
            }
            compilation_error_t *error = create_error_from_token(arena, opening_token);
            wchar_t *message = format_string_to_arena(arena, &error->message_length,
                get_messages()->unclosed_opening_bracket, opening_token->text[0]);
            error->end = previous->end;
            error->message = message;
            return error;
        }
        if (token->type == TOKEN_BRACKET) {
            wchar_t bracket = token->text[0];
            if (bracket == L'(' || bracket == L'{' || bracket == '[') {
                token_t *bracket_pair = (token_t *)alloc_zeroed_from_arena(arena, sizeof(token_t));
                bracket_pair->type = TOKEN_BRACKET_PAIR;
                bracket_pair->begin = token->begin;
                const token_t *last_token;
                compilation_error_t *error = scan_and_analyze_for_brackets(arena, scan,
                    &bracket_pair->child_tokens, token, &last_token);
                if (error != NULL) {
                    return error;
                }
                bracket_pair->end = last_token->end;
            } else {
                return NULL;
            }
        } else {
            append_token_to_neighbors(list, token);
        }
    }
    return NULL;
}

token_list_t *process_brackets(arena_t *arena, scanner_t *scan) {
    token_list_t *tokens = (token_list_t *)alloc_zeroed_from_arena(scan->tokens_memory,
        sizeof(token_list_t));
    scan_and_analyze_for_brackets(arena, scan, tokens, NULL, NULL);
    return tokens;
}
