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
#include "graph/node.h"

/**
 * @brief Rule for handling an additive operator (`+` or `-`), followed by expressions
 *  on both sides.
 */
compilation_error_t *parsing_additive_operators(token_t *operator, parser_memory_t *memory);

/**
 * @brief Rule for handling an identifier followed by parentheses (function call).
 */
compilation_error_t *parsing_identifier_and_parentheses(token_t *identifier,
    parser_memory_t *memory);

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
 * the token list and the syntax tree by creating new nodes or tokens. If a reduction
 * rule encounters an error, it is added to a linked list of errors, which the function
 * returns as its result. If a critical error is encountered, the function stops processing
 * immediately.
 * 
 * @param list A pointer to the token list to which the reduction rule will be applied.
 * @param rule The reduction rule function that will be applied to the tokens.
 * @param memory A pointer to the `parser_memory_t` structure, which manages memory allocation
 *  for tokens and syntax tree nodes.
 * @param error A pointer to an existing linked list of errors. Any new errors found during
 *  the reduction process will be added to this list.
 * @return A pointer to the updated linked list of `compilation_error_t` structures. If no
 *  errors were encountered, the function returns the original `error` pointer (unchanged).
 * 
 * @note The function maintains the order of errors by prepending new errors to the front
 *  of the list. The most recent error will be at the head of the returned list.
 * @note If a critical error is encountered, the function stops processing immediately and 
 *  returns the updated error list.
 */
static compilation_error_t * apply_reduction_rule_forward(token_list_t *list, reduce_rule_t rule,
        parser_memory_t *memory, compilation_error_t *error) {
    token_t *token = list->first;
    while (token != NULL) {
        token_t *next = token->next_in_group;
        compilation_error_t *new_error = rule(token, memory);
        if (new_error != NULL) {
            new_error->next = error;
            error = new_error;
            if (error->critical) {
                break;
            }
        }
        token = next;
    }
    return error;
}

/**
 * @brief Applies a reduction rule to the token list from the last to the first token.
 * 
 * This function traverses the token list in reverse order, starting from the last token and
 * moving to the first, applying the provided reduction rule to each token. The reduction rule
 * may modify the token list and the syntax tree by creating new nodes or tokens. If a reduction
 * rule encounters an error, it is added to a linked list of errors, which the function updates.
 * If a critical error is encountered, the function stops processing immediately.
 * 
 * @param list A pointer to the token list to which the reduction rule will be applied.
 * @param rule The reduction rule function that will be applied to the tokens.
 * @param memory A pointer to the `parser_memory_t` structure, which manages memory allocation
 *  for tokens, syntax tree nodes, and errors.
 * @param error A pointer to an existing linked list of errors. Any new errors found during
 *  the reduction process will be added to this list.
 * @return A pointer to the updated linked list of `compilation_error_t` structures. If no
 *  errors were encountered, the function returns the original `error` pointer (unchanged).
 * 
 * @note The function maintains the order of errors by prepending new errors to the front
 *  of the list. The most recent error will be at the head of the returned list.
 * @note If a critical error is encountered, the function stops processing immediately and 
 *  returns the updated error list.
 * @note Memory for error structures is allocated from the same arena as tokens (`memory->tokens`), 
 *  ensuring centralized memory management.
 */
static void apply_reduction_rule_backward(token_list_t *list, reduce_rule_t rule,
        parser_memory_t *memory, compilation_error_t *error) {
    token_t *token = list->last;
    while (token != NULL) {
        token_t *previous = token->previous_in_group;
        compilation_error_t *new_error = rule(token, memory);
        if (new_error != NULL) {
            new_error->next = error;
            error = new_error;
            if (error->critical) {
                break;
            }
        }
        token = previous;
    }
}

compilation_error_t *process_brackets(arena_t *arena, scanner_t *scan, token_list_t *tokens) {
    memset(tokens, 0, sizeof(token_list_t));
    const token_t *last_token;
    compilation_error_t *error = scan_and_analyze_for_brackets(arena, scan, tokens, NULL,
        &last_token);
    if (error != NULL) {
        error->critical = true;
    }
    return error;
}

token_t *collapse_tokens_to_token(arena_t *arena, token_t *first, token_t *last,
        token_type_t type, node_t *node) {
    token_t *new_token = (token_t *)alloc_zeroed_from_arena(arena, sizeof(token_t));
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

statement_list_processing_result_t process_statement_list(parser_memory_t *memory,
        token_list_t *tokens) {
    statement_list_processing_result_t result = {0};
    result.list = (statement_t **)alloc_from_arena(memory->tokens,
        tokens->count * sizeof(statement_t *));
    token_t *token = tokens->first;
    while (token != NULL) {
        if (token->type == TOKEN_STATEMENT) {
            result.list[result.count++] = (statement_t *)token->node;
        }
        else if (token->type == TOKEN_EXPRESSION) {
            result.list[result.count++] = create_statement_expression_node(memory->graph,
                (expression_t*)token->node);
        }
        else {
            result.error = create_error_from_token(memory->tokens, token,
                get_messages()->not_a_statement, token->text);
            break;
        }
        token = token->right;
    }
    return result;
}

compilation_error_t *apply_reduction_rules(token_groups_t *groups, parser_memory_t *memory) {
    compilation_error_t *error = NULL;
    error = apply_reduction_rule_forward(&groups->additive_operators, parsing_additive_operators,
        memory, error);
    if (error != NULL && error->critical) {
        return error;
    }
    error = apply_reduction_rule_forward(&groups->identifiers, parsing_identifier_and_parentheses,
        memory, error);
    if (error != NULL && error->critical) {
        return error;
    }
    // add other rules...
    return error;
}

compilation_error_t *process_root_token_list(parser_memory_t *memory,
        token_list_t *tokens, node_t **root_node) {
    statement_list_processing_result_t stmt = process_statement_list(memory, tokens);
    if (stmt.error != NULL) {
        *root_node = NULL;
        return stmt.error;
    } else {
        *root_node = create_root_node(memory->graph, stmt.list, stmt.count);
        return NULL;
    }
}
