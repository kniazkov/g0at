/** @file native_strings.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native string operations provided by libgoat_native.a, independent of VM objects.
 * Input views must be valid and no longer than GOAT_NATIVE_STRING_MAX. Producer output
 * buffers must be empty and distinct from inputs; errors leave output unchanged.
 * Locale-dependent operations require host string services; allocation uses host memory
 * services. The caller performs language-object/character-to-string conversions.
 */
#pragma once
#ifndef GOAT_NATIVE_STRINGS_V2_H
#    define GOAT_NATIVE_STRINGS_V2_H
#    include "native_abi.h"
#    include "unicode.h"

#    include <stdbool.h>

/** @brief Largest materializable string length, reserving its terminating code unit. */
#    define GOAT_NATIVE_STRING_MAX (SIZE_MAX / sizeof(gchar_t) - 1)

#    ifdef __cplusplus
extern "C" {
#    endif
/** @brief Borrows a buffer; the view does not extend its lifetime. */
static inline goat_native_string_view_v2_t
goat_native_string_view(goat_native_string_buffer_v2_t buffer) {
    return (goat_native_string_view_v2_t){buffer.data, buffer.length};
}

/** @brief Releases a buffer through its original context and resets its fields. */
static inline void goat_native_string_release(const goat_native_context_v2_t *context,
                                              goat_native_string_buffer_v2_t *buffer) {
    goat_native_deallocate(context, buffer->data);
    *buffer = (goat_native_string_buffer_v2_t){0};
}

/** @brief Copies a bounded view into context-owned storage. */
uint32_t goat_native_string_copy(const goat_native_context_v2_t *context,
                                 goat_native_string_view_v2_t value,
                                 goat_native_string_buffer_v2_t *out);

/** @brief Returns the VM-compatible length in gchar_t code units. */
int64_t goat_native_string_length(goat_native_string_view_v2_t value);

/** @brief Tests emptiness without dereferencing the data pointer. */
bool goat_native_string_is_empty(goat_native_string_view_v2_t value);

/** @brief Returns one code unit as a character; invalid indices leave out unchanged. */
uint32_t
goat_native_string_char_at(goat_native_string_view_v2_t value, int64_t index, uint32_t *out);

/** @brief Copies [begin, end); callers use length as end for the one-index form. */
uint32_t goat_native_string_substring(const goat_native_context_v2_t *context,
                                      goat_native_string_view_v2_t value,
                                      int64_t begin,
                                      int64_t end,
                                      goat_native_string_buffer_v2_t *out);

/** @brief Finds the first occurrence, including embedded zero units; -1 if absent. */
int64_t goat_native_string_index_of(goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t needle);

/** @brief Finds the last occurrence; the empty needle matches at length. */
int64_t goat_native_string_last_index_of(goat_native_string_view_v2_t value,
                                         goat_native_string_view_v2_t needle);

/** @brief Tests substring presence. */
bool goat_native_string_contains(goat_native_string_view_v2_t value,
                                 goat_native_string_view_v2_t needle);

/** @brief Tests a prefix; every string starts with the empty string. */
bool goat_native_string_starts_with(goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t prefix);

/** @brief Tests a suffix; every string ends with the empty string. */
bool goat_native_string_ends_with(goat_native_string_view_v2_t value,
                                  goat_native_string_view_v2_t suffix);

/** @brief Native str for an integer; output is owned by the invocation context. */
uint32_t goat_native_str_i64(const goat_native_context_v2_t *context,
                             int64_t value,
                             goat_native_string_buffer_v2_t *out);
/** @brief Native str for a real, using the same formatting as the interpreter. */
uint32_t goat_native_str_f64(const goat_native_context_v2_t *context,
                             double value,
                             goat_native_string_buffer_v2_t *out);
/** @brief Native str for a boolean. */
uint32_t goat_native_str_bool(const goat_native_context_v2_t *context,
                              bool value,
                              goat_native_string_buffer_v2_t *out);
/** @brief Native str for a character, preserving host gchar_t encoding and embedded NUL. */
uint32_t goat_native_str_char(const goat_native_context_v2_t *context,
                              uint32_t value,
                              goat_native_string_buffer_v2_t *out);

/** @brief Concatenates two views without modifying either operand. */
uint32_t goat_native_string_concat(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t left,
                                   goat_native_string_view_v2_t right,
                                   goat_native_string_buffer_v2_t *out);

/** @brief Checks availability of host-local character classification and case mapping. */
static inline bool goat_native_has_string_services(const goat_native_context_v2_t *context) {
    return context && context->services
           && context->services->struct_size >= GOAT_NATIVE_STRING_SERVICES_SIZE
           && context->services->string_is_space && context->services->string_to_upper
           && context->services->string_to_lower;
}

/** @brief Tests whitespace using Unicode 17.0.0; empty strings are blank. */
uint32_t goat_native_string_is_blank(const goat_native_context_v2_t *context,
                                     goat_native_string_view_v2_t value,
                                     bool *out);

/** @brief Copies the string after removing leading and trailing Unicode White_Space. */
uint32_t goat_native_string_trim(const goat_native_context_v2_t *context,
                                 goat_native_string_view_v2_t value,
                                 goat_native_string_buffer_v2_t *out);

/** @brief Converts to uppercase with the same full Unicode mapping as the VM. */
uint32_t goat_native_string_to_upper_case(const goat_native_context_v2_t *context,
                                          goat_native_string_view_v2_t value,
                                          goat_native_string_buffer_v2_t *out);

/** @brief Converts to lowercase with the same full Unicode mapping as the VM. */
uint32_t goat_native_string_to_lower_case(const goat_native_context_v2_t *context,
                                          goat_native_string_view_v2_t value,
                                          goat_native_string_buffer_v2_t *out);

/** @brief Repeats a string; negative counts and unrepresentable sizes are invalid. */
uint32_t goat_native_string_repeat(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t value,
                                   int64_t count,
                                   goat_native_string_buffer_v2_t *out);

/** @brief Replaces non-overlapping occurrences; an empty old part leaves content unchanged. */
uint32_t goat_native_string_replace(const goat_native_context_v2_t *context,
                                    goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t old_part,
                                    goat_native_string_view_v2_t new_part,
                                    goat_native_string_buffer_v2_t *out);

/** @brief Formats %0, %1, ... using already converted argument views.
 * Missing or overflowing indices remain literal, as in the VM. Conversion of arbitrary
 * language values to strings belongs to the caller; this layer never touches VM objects.
 */
uint32_t goat_native_string_format(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t format,
                                   const goat_native_string_view_v2_t *arguments,
                                   size_t count,
                                   goat_native_string_buffer_v2_t *out);
#    ifdef __cplusplus
}
#    endif
#endif
