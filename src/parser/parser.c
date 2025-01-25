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

#include <assert.h>
#include <stdbool.h>
#include <memory.h>

#include "parser.h"
#include "compilation_error.h"
#include "lib/arena.h"
#include "scanner/scanner.h"
#include "resources/messages.h"

/**
 * @brief Scans and analyzes tokens for balanced brackets, transforming nested brackets into
 *  a special token.
 * 
 * This function iterates through tokens and checks for matching opening and closing brackets.
 * It wraps balanced bracket pairs into a special `TOKEN_BRACKET_PAIR` token.
 * If an unmatched bracket is found, or if brackets do not match, it returns an error.
 * 
 * @param arena The memory arena for allocating new tokens and error descriptors.
 * @param scan The scanner used to get tokens.
 * @param list The list to which the resulting tokens will be added.
 * @param opening_token The token representing the opening bracket (used to track errors).
 * @param closing_token A pointer to where the closing token will be stored if found.
 * 
 * @return A `compilation_error_t` pointer if an error is detected (e.g., mismatched brackets),
 *  or NULL if no errors.
 */
static compilation_error_t *scan_and_analyze_for_brackets(arena_t *arena, scanner_t *scan,
        token_list_t *list, const token_t *opening_token, const token_t **closing_token) {
    const token_t *previous = opening_token;
    while(true) {
        token_t *token = get_token(scan);
        if (token == NULL) {
            if (opening_token == NULL) {
                return NULL; // no opening bracket - no error
            }
            compilation_error_t *error = create_error_from_token(arena, opening_token,
                get_messages()->unclosed_opening_bracket, opening_token->text[0]);
            error->end = previous->end;
            return error;
        }
        if (token->type == TOKEN_ERROR) {
            return create_error_from_token(arena, token, NULL);
        }
        if (token->type == TOKEN_BRACKET) {
            wchar_t bracket = token->text[0];
            if (bracket == L'(' || bracket == L'{' || bracket == '[') {
                token_t *pair = (token_t *)alloc_zeroed_from_arena(arena, sizeof(token_t));
                pair->type = TOKEN_BRACKET_PAIR;
                pair->begin = token->begin;
                compilation_error_t *error = scan_and_analyze_for_brackets(arena, scan,
                    &pair->children, token, &previous);
                if (error != NULL) {
                    return error;
                }
                pair->end = previous->end;
                wchar_t *text = (wchar_t *)alloc_from_arena(arena, sizeof(wchar_t) * 3);
                text[0] = bracket;
                text[1] = previous->text[0];
                text[2] = L'\0';
                pair->text = text;
                pair->length = 2;
                append_token_to_neighbors(list, pair);
            } else {
                *closing_token = token;
                if (opening_token == NULL) {
                    return create_error_from_token(arena, token,
                        get_messages()->missing_opening_bracket, bracket);
                }
                wchar_t opening_bracket = L'\0';
                if (bracket == L')') {
                    opening_bracket = L'(';
                } else if (bracket == L']') {
                    opening_bracket = L'[';
                } else if (bracket == L'}') {
                    opening_bracket = L'{';
                }
                assert(opening_bracket != L'\0');
                if (opening_token->text[0] != opening_bracket) {
                    compilation_error_t *error = create_error_from_token(arena, opening_token,
                        get_messages()->brackets_do_not_match, bracket, opening_token->text[0]);
                    error->end = token->end;
                    return error;
                }
                return NULL;
            }
        } else {
            append_token_to_neighbors(list, token);
            previous = token;
        }
    }
    return NULL;
}

compilation_error_t *process_brackets(arena_t *arena, scanner_t *scan, token_list_t *tokens) {
    memset(tokens, 0, sizeof(token_list_t));
    const token_t *last_token;
    return scan_and_analyze_for_brackets(arena, scan, tokens, NULL, &last_token);
}
