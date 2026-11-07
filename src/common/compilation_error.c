/**
 * @file compilation_error.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides helper functions for allocating and populating compilation diagnostics.
 */

#include "compilation_error.h"

#include "graph/node.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "lib/strings/string_ext.h"
#include "resources/messages.h"
#include "scanner/token.h"

#include <assert.h>
#include <stdarg.h>

/** @brief Creates a compilation diagnostic from a source range and a variable argument list. */
static compilation_error_t *create_error_from_position_vargs(arena_t *arena,
                                                             position_range_t *position,
                                                             compilation_error_severity_t severity,
                                                             const gchar_t *format,
                                                             va_list args) {
    assert(format != NULL);

    compilation_error_t *error =
        (compilation_error_t *)alloc_from_arena(arena, sizeof(compilation_error_t));
    error->position = position;
    string_value_t value = format_string_vargs(format, args);
    if (value.data != NULL) {
        error->message = copy_string_to_arena(arena, value.data, value.length);
        FREE_STRING(value);
    } else {
        error->message = EMPTY_STRING_VIEW;
    }
    error->severity = severity;
    error->next = NULL;
    return error;
}

compilation_error_t *create_error_from_token(arena_t *arena,
                                             const token_t *token,
                                             compilation_error_severity_t severity,
                                             const gchar_t *format,
                                             ...) {
    compilation_error_t *error;
    va_list args;

    va_start(args, format);
    error = create_error_from_position_vargs(arena, token->position, severity, format, args);
    va_end(args);

    return error;
}

compilation_error_t *create_error_from_node(arena_t *arena,
                                            const node_t *node,
                                            compilation_error_severity_t severity,
                                            const gchar_t *format,
                                            ...) {
    compilation_error_t *error;
    va_list args;

    va_start(args, format);
    error = create_error_from_position_vargs(arena, node->position, severity, format, args);
    va_end(args);

    return error;
}

compilation_error_t *reverse_compilation_errors(compilation_error_t *head) {
    compilation_error_t *prev = NULL;
    compilation_error_t *curr = head;

    while (curr) {
        compilation_error_t *next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    return prev;
}

compilation_error_severity_t get_most_severe_compilation_error(const compilation_error_t *head) {
    compilation_error_severity_t result = WARNING;

    while (head) {
        if (head->severity > result) {
            result = head->severity;
            if (result == CRITICAL) {
                return result;
            }
        }
        head = head->next;
    }

    return result;
}

string_value_t format_compilation_errors(const compilation_error_t *error) {
    size_t count = 0;
    for (const compilation_error_t *p = error; p; p = p->next)
        ++count;
    const compilation_error_t **items = count ? ALLOC(count * sizeof(*items)) : NULL;
    size_t i = 0;
    for (const compilation_error_t *p = error; p; p = p->next)
        items[i++] = p;
    string_builder_t builder = {0};
    string_value_t text = NULL_STRING_VALUE;
    while (i) {
        const compilation_error_t *p = items[--i];
        const gchar_t *format = p->severity == WARNING ? get_messages()->compilation_warning
                                : p->severity == CRITICAL
                                    ? get_messages()->critical_compilation_error
                                    : get_messages()->compilation_error;
        string_value_t line = format_string(format,
                                            p->position->begin->file_name,
                                            p->position->begin->row,
                                            p->position->begin->column,
                                            p->message.data);
        append_string_value(&builder, line);
        FREE_STRING(line);
        text = append_char(&builder, '\n');
    }
    FREE(items);
    return text;
}
