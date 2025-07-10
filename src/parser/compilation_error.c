/**
 * @file compilation_error.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides helper functions for allocating and populating compilation error structures.
 *
 * This file contains functions for creating and managing `compilation_error_t` structures,
 * which represent errors that occur during compilation. These errors include information
 * such as the position of the error, a descriptive error message, and the length of the message.
 */

#include <assert.h>

#include "compilation_error.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "scanner/token.h"


compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token,
        const wchar_t *format, ...) {
    assert(format != NULL);
    compilation_error_t *error = (compilation_error_t *)alloc_from_arena(
            arena, sizeof(compilation_error_t));
    error->begin = token->begin;
    error->end = token->end;
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    if (value.data != NULL) {
        error->message = copy_string_to_arena(arena, value.data, value.length);
        if (value.should_free) {
            FREE(value.data);
        }
    } else {
        error->message = (string_view_t){ L"", 0 };
    }
    error->critical = false;
    error->next = NULL;
    return error;
}
