/**
 * @file parser.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures and function prototypes for the parser.
 */

#pragma once

#include "common/compilation_error.h"
#include "graph/statement.h"
#include "lib/linked_list.h"
#include "scanner/scanner.h"

/** @brief Partial result of the parsing process. */
typedef struct {
    /** @brief List of parsed function objects. */
    list_t *functions;
} parsing_result_t;

/**
 * @brief Function pointer type for token reduction rules in a bottom-up parser.
 * @return A pointer to a `compilation_error_t` if a syntax error occurs, or `NULL` if the rule was
 * successfully applied.
 */
typedef compilation_error_t *(*reduce_rule_t)(token_t *start_token,
                                              parser_memory_t *memory,
                                              token_groups_t *groups);

/**
 * @brief Processes tokens and analyzes bracket pairs, storing the result in the provided token
 * list.
 * @return A `compilation_error_t` pointer if an error is detected (e.g., mismatched brackets), or
 * NULL if no errors.
 */
compilation_error_t *process_brackets(parser_memory_t *memory,
                                      scanner_t *scan,
                                      token_list_t *tokens,
                                      token_groups_t *groups);

/**
 * @brief Replaces the inclusive token range with one token linked to node.
 * The old tokens remain arena-owned; list links are updated.
 */
token_t *collapse_tokens_to_token(parser_memory_t *memory,
                                  token_t *first,
                                  token_t *last,
                                  token_type_t type,
                                  node_t *node);

/** @brief Result of processing tokens into a statement list. */
typedef struct {
    /** @brief Linked list of statement nodes. */
    list_t *list;

    /** @brief A pointer to a compilation error, if any. */
    compilation_error_t *error;
} statement_list_processing_result_t;

/** @brief Processes tokens and builds a linked list of statements. */
statement_list_processing_result_t process_statement_list(parser_memory_t *memory,
                                                          token_list_t *tokens);
/**
 * @brief Applies syntax reductions and collects functions for deferred generation.
 * May collect multiple diagnostics; critical errors stop processing.
 * @return Linked diagnostics, or NULL on success.
 */
compilation_error_t *
apply_reduction_rules(token_groups_t *groups, parser_memory_t *memory, parsing_result_t *result);

/**
 * @brief Processes the root-level token list and constructs a syntax tree root node.
 * @return A `compilation_error_t` structure containing any compilation error encountered during the
 * processing, or `NULL` if no error occurred.
 */
compilation_error_t *
process_root_token_list(parser_memory_t *memory, token_list_t *tokens, node_t **root_node);
