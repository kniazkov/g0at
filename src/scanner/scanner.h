/**
 * @file scanner.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides the definition of the scanner structure and function prototypes for lexical
 * analysis.
 */

#pragma once

#include "goat/unicode.h"
#include "token.h"

typedef struct scanner_t scanner_t;
typedef struct parser_memory_t parser_memory_t;

/**
 * @brief The scanner for lexical analysis.
 *
 * Holds the necessary state for performing lexical analysis, including the current character in the
 * source code, the current position in the file, a memory manager for tokens and syntax tree nodes,
 * and a pointer to token groups organized by type or role.
 */
struct scanner_t {
    /** @brief Code processed by the scanner. */
    gchar_t *code;

    /** @brief Explicit source length; embedded zeros are not EOF. */
    size_t length;

    /** @brief The current position in the source code. */
    full_position_t position;

    /** @brief Pointer to the memory manager used for token and syntax tree node allocation. */
    parser_memory_t *memory;

    /**
     * @brief Pointer to token groups organized by type or role.
     *
     * The groups are typically created outside the scanner and passed to it for filling during
     * lexical analysis.
     */
    token_groups_t *groups;
};

/**
 * @brief Creates a new scanner for lexical analysis.
 * Copies the length-delimited source into the token arena, then blanks comments
 * while retaining line positions. The input string remains caller-owned. The
 * filename and memory/groups objects are borrowed for scanning and diagnostics;
 * groups is reset on creation. Embedded zero characters are not end-of-source.
 */
scanner_t *create_scanner(const char *file_name,
                          string_value_t code,
                          parser_memory_t *memory,
                          token_groups_t *groups);

/**
 * @brief Extracts the next token from the source code.
 *
 * When there are no more tokens, the function returns NULL.
 * @return A pointer to a `token_t` representing the next token, or `NULL` if no tokens are left.
 */
token_t *get_token(scanner_t *scan);
