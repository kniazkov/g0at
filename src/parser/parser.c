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
#include "scanner/scanner.h"
#include "lib/arena.h"

static void scan_and_analyze_for_brackets(scanner_t *scan, token_list_t *list, wchar_t closing_bracket) {
    while(true) {
        token_t *token = get_token(scan);
        if (token->type == TOKEN_BRACKET) {
            wchar_t bracket = token->text[0];
            if (bracket == L'(' || bracket == L'{' || bracket == '[') {
                
            } else {
                if (bracket != closing_bracket) {
                    // error;
                }
                return;
            }
        } else {
            append_token_to_neighbors(list, token);
        }
    }
}

token_list_t *process_brackets(scanner_t *scan) {
    token_list_t *tokens = (token_list_t *)alloc_zeroed_from_arena(scan->tokens_memory,
        sizeof(token_list_t));
    scan_and_analyze_for_brackets(scan, tokens, 0);
    return tokens;
}
