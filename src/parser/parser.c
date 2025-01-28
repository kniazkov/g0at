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
#include "scanner/token.h"
#include "resources/messages.h"

/**
 * @brief Rule for handling an identifier followed by parentheses (function call).
 */
void identifier_and_parentheses(token_t *start_token, arena_t *tokens_memory,
        arena_t *graph_memory);

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

/**
 * @brief Applies a reduction rule to the token list from the first to the last token.
 * 
 * This function traverses the token list from the first token to the last, applying
 * the provided reduction rule to each token in the list. The reduction rule may modify
 * the token list and the syntax tree by creating new nodes or tokens.
 * 
 * @param list A pointer to the token list to which the reduction rule will be applied.
 * @param rule The reduction rule function that will be applied to the tokens.
 * @param tokens_memory The memory arena for allocating new tokens during reduction.
 * @param graph_memory The memory arena for allocating new syntax tree nodes during reduction.
 */
static void apply_reduction_rule_forward(token_list_t *list, reduce_rule_t rule,
                                  arena_t *tokens_memory, arena_t *graph_memory) {
    token_t *token = list->first;
    while (token != NULL) {
        token_t *next = token->next_in_group;
        rule(token, tokens_memory, graph_memory);
        token = next;
    }
}

/**
 * @brief Applies a reduction rule to the token list from the last to the first token.
 * 
 * This function traverses the token list from the last token to the first, applying
 * the provided reduction rule to each token in the list. The reduction rule may modify
 * the token list and the syntax tree by creating new nodes or tokens.
 * 
 * @param list A pointer to the token list to which the reduction rule will be applied.
 * @param rule The reduction rule function that will be applied to the tokens.
 * @param tokens_memory The memory arena for allocating new tokens during reduction.
 * @param graph_memory The memory arena for allocating new syntax tree nodes during reduction.
 */
static void apply_reduction_rule_backward(token_list_t *list, reduce_rule_t rule,
                                   arena_t *tokens_memory, arena_t *graph_memory) {
    token_t *token = list->last;

    while (token != NULL) {
        token_t *previous= token->previous_in_group;
        rule(token, tokens_memory, graph_memory);
        token = previous;
    }
}

compilation_error_t *process_brackets(arena_t *arena, scanner_t *scan, token_list_t *tokens) {
    memset(tokens, 0, sizeof(token_list_t));
    const token_t *last_token;
    return scan_and_analyze_for_brackets(arena, scan, tokens, NULL, &last_token);
}

token_t *collapse_tokens_to_token(arena_t *tokens_memory, token_t *first, token_t *last,
        token_type_t type, node_t *node) {
    token_t *new_token = (token_t *)alloc_zeroed_from_arena(tokens_memory, sizeof(token_t));
    new_token->type = type;
    new_token->begin = first->begin;
    new_token->end = last->end;
    new_token->node = node;
    token_t *old_token = first;
    while(old_token != last) {
        token_t *next = old_token->right;
        remove_token(old_token);
        old_token = next;
    }
    replace_token(old_token, new_token);
    return new_token;
}

void apply_reduction_rules(token_groups_t *groups, arena_t *tokens_memory, arena_t *graph_memory) {
    apply_reduction_rule_forward(&groups->identifiers, identifier_and_parentheses,
            tokens_memory, graph_memory);
    // add other rules...
}
