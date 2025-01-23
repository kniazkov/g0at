/**
 * @file compilation_error.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides helper functions for allocating and populating compilation error structures.
 *
 * This file contains functions for creating and managing `compilation_error_t` structures,
 * which represent errors that occur during compilation. These errors include information
 * such as the position of the error, a descriptive error message, and the length of the message.
 */

#include "compilation_error.h"
#include "lib/arena.h"
#include "scanner/token.h"

compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token) {
    compilation_error_t *error = (compilation_error_t *)alloc_from_arena(
            arena, sizeof(compilation_error_t));
    error->begin = token->begin;
    error->end = token->end;
    error->message = token->text;
    error->message_length = token->length;
}
