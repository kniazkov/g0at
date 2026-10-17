/** @file strings.c
 * @copyright 2026 Ivan Kniazkov
 * @brief String algorithms linked into native modules from the static support library.
 */
#include "goat/native_strings.h"

/** @brief Internal allocation. Outputs must be empty; failures leave them unchanged.
 * All input views must satisfy the ABI contract and length <= GOAT_NATIVE_STRING_MAX.
 * OOM is fatal in the host; unrepresentable operation sizes return BAD_REQUEST.
 */
static uint32_t goat_native_string_storage(const goat_native_context_v2_t *context,
                                           size_t length,
                                           goat_native_string_buffer_v2_t *out) {
    if (length > GOAT_NATIVE_STRING_MAX)
        return GOAT_NATIVE_BAD_REQUEST;
    if (!goat_native_has_memory_services(context))
        return GOAT_NATIVE_ABI_MISMATCH;
    goat_native_string_buffer_v2_t result = {0};
    if (length) {
        result.data = goat_native_allocate(context, (length + 1) * sizeof(wchar_t));
        result.length = result.capacity = length;
        result.data[length] = 0;
    }
    *out = result;
    return GOAT_NATIVE_OK;
}

/** @brief Copies a bounded view into context-owned storage. */
uint32_t goat_native_string_copy(const goat_native_context_v2_t *context,
                                 goat_native_string_view_v2_t value,
                                 goat_native_string_buffer_v2_t *out) {
    uint32_t status = goat_native_string_storage(context, value.length, out);
    if (status == GOAT_NATIVE_OK && value.length)
        wmemcpy(out->data, value.data, value.length);
    return status;
}

/** @brief Returns the VM-compatible length in wchar_t code units. */
int64_t goat_native_string_length(goat_native_string_view_v2_t value) {
    return (int64_t)value.length;
}

/** @brief Tests emptiness without dereferencing the data pointer. */
bool goat_native_string_is_empty(goat_native_string_view_v2_t value) {
    return value.length == 0;
}

/** @brief Returns one code unit as a character; invalid indices leave out unchanged. */
uint32_t
goat_native_string_char_at(goat_native_string_view_v2_t value, int64_t index, uint32_t *out) {
    if (index < 0 || (uint64_t)index >= value.length)
        return GOAT_NATIVE_BAD_REQUEST;
    *out = (uint32_t)value.data[(size_t)index];
    return GOAT_NATIVE_OK;
}

/** @brief Copies [begin, end); callers use length as end for the one-index form. */
uint32_t goat_native_string_substring(const goat_native_context_v2_t *context,
                                      goat_native_string_view_v2_t value,
                                      int64_t begin,
                                      int64_t end,
                                      goat_native_string_buffer_v2_t *out) {
    if (begin < 0 || end < begin || (uint64_t)end > value.length)
        return GOAT_NATIVE_BAD_REQUEST;
    goat_native_string_view_v2_t part = {NULL, (size_t)(end - begin)};
    if (part.length)
        part.data = value.data + (size_t)begin;
    return goat_native_string_copy(context, part, out);
}

/** @brief Finds the first occurrence, including embedded zero units; -1 if absent. */
int64_t goat_native_string_index_of(goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t needle) {
    if (!needle.length)
        return 0;
    if (needle.length > value.length)
        return -1;
    for (size_t i = 0; i <= value.length - needle.length; ++i)
        if (!wmemcmp(value.data + i, needle.data, needle.length))
            return (int64_t)i;
    return -1;
}

/** @brief Finds the last occurrence; the empty needle matches at length. */
int64_t goat_native_string_last_index_of(goat_native_string_view_v2_t value,
                                         goat_native_string_view_v2_t needle) {
    if (!needle.length)
        return (int64_t)value.length;
    if (needle.length > value.length)
        return -1;
    size_t i = value.length - needle.length;
    do {
        if (!wmemcmp(value.data + i, needle.data, needle.length))
            return (int64_t)i;
    } while (i-- != 0);
    return -1;
}

/** @brief Tests substring presence. */
bool goat_native_string_contains(goat_native_string_view_v2_t value,
                                 goat_native_string_view_v2_t needle) {
    return goat_native_string_index_of(value, needle) >= 0;
}

/** @brief Tests a prefix; every string starts with the empty string. */
bool goat_native_string_starts_with(goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t prefix) {
    return !prefix.length
           || (prefix.length <= value.length && !wmemcmp(value.data, prefix.data, prefix.length));
}

/** @brief Tests a suffix; every string ends with the empty string. */
bool goat_native_string_ends_with(goat_native_string_view_v2_t value,
                                  goat_native_string_view_v2_t suffix) {
    return !suffix.length
           || (suffix.length <= value.length
               && !wmemcmp(value.data + value.length - suffix.length, suffix.data, suffix.length));
}

/** @brief Concatenates two views without modifying either operand. */
uint32_t goat_native_string_concat(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t left,
                                   goat_native_string_view_v2_t right,
                                   goat_native_string_buffer_v2_t *out) {
    if (left.length > GOAT_NATIVE_STRING_MAX || right.length > GOAT_NATIVE_STRING_MAX - left.length)
        return GOAT_NATIVE_BAD_REQUEST;
    uint32_t status = goat_native_string_storage(context, left.length + right.length, out);
    if (status != GOAT_NATIVE_OK)
        return status;
    if (left.length)
        wmemcpy(out->data, left.data, left.length);
    if (right.length)
        wmemcpy(out->data + left.length, right.data, right.length);
    return GOAT_NATIVE_OK;
}

/** @brief Tests whitespace in the interpreter's locale; empty strings are blank. */
uint32_t goat_native_string_is_blank(const goat_native_context_v2_t *context,
                                     goat_native_string_view_v2_t value,
                                     bool *out) {
    if (!goat_native_has_string_services(context))
        return GOAT_NATIVE_ABI_MISMATCH;
    bool result = true;
    for (size_t i = 0; i < value.length; ++i)
        if (!context->services->string_is_space(value.data[i])) {
            result = false;
            break;
        }
    *out = result;
    return GOAT_NATIVE_OK;
}

/** @brief Copies the string after removing leading and trailing host-locale whitespace. */
uint32_t goat_native_string_trim(const goat_native_context_v2_t *context,
                                 goat_native_string_view_v2_t value,
                                 goat_native_string_buffer_v2_t *out) {
    if (!goat_native_has_string_services(context))
        return GOAT_NATIVE_ABI_MISMATCH;
    size_t begin = 0, end = value.length;
    while (begin < end && context->services->string_is_space(value.data[begin]))
        ++begin;
    while (end > begin && context->services->string_is_space(value.data[end - 1]))
        --end;
    return goat_native_string_substring(context, value, (int64_t)begin, (int64_t)end, out);
}

/** @brief Internal one-code-unit case conversion using the interpreter's CRT and locale. */
static uint32_t goat_native_string_case(const goat_native_context_v2_t *context,
                                        goat_native_string_view_v2_t value,
                                        bool upper,
                                        goat_native_string_buffer_v2_t *out) {
    if (!goat_native_has_string_services(context))
        return GOAT_NATIVE_ABI_MISMATCH;
    uint32_t status = goat_native_string_storage(context, value.length, out);
    if (status == GOAT_NATIVE_OK)
        for (size_t i = 0; i < value.length; ++i)
            out->data[i] = upper ? context->services->string_to_upper(value.data[i])
                                 : context->services->string_to_lower(value.data[i]);
    return status;
}

/** @brief Converts to uppercase with the same one-unit mapping as the VM. */
uint32_t goat_native_string_to_upper_case(const goat_native_context_v2_t *context,
                                          goat_native_string_view_v2_t value,
                                          goat_native_string_buffer_v2_t *out) {
    return goat_native_string_case(context, value, true, out);
}

/** @brief Converts to lowercase with the same one-unit mapping as the VM. */
uint32_t goat_native_string_to_lower_case(const goat_native_context_v2_t *context,
                                          goat_native_string_view_v2_t value,
                                          goat_native_string_buffer_v2_t *out) {
    return goat_native_string_case(context, value, false, out);
}

/** @brief Repeats a string; negative counts and unrepresentable sizes are invalid. */
uint32_t goat_native_string_repeat(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t value,
                                   int64_t count,
                                   goat_native_string_buffer_v2_t *out) {
    if (count < 0 || (value.length && (uint64_t)count > GOAT_NATIVE_STRING_MAX / value.length))
        return GOAT_NATIVE_BAD_REQUEST;
    size_t length = value.length ? value.length * (size_t)count : 0;
    uint32_t status = goat_native_string_storage(context, length, out);
    if (status == GOAT_NATIVE_OK && value.length)
        for (size_t i = 0; i < length; i += value.length)
            wmemcpy(out->data + i, value.data, value.length);
    return status;
}

/** @brief Replaces non-overlapping occurrences; an empty old part leaves content unchanged. */
uint32_t goat_native_string_replace(const goat_native_context_v2_t *context,
                                    goat_native_string_view_v2_t value,
                                    goat_native_string_view_v2_t old_part,
                                    goat_native_string_view_v2_t new_part,
                                    goat_native_string_buffer_v2_t *out) {
    if (!old_part.length)
        return goat_native_string_copy(context, value, out);
    size_t length = 0;
    /* The first pass measures; the second writes. No intermediate allocation is needed. */
    for (int pass = 0; pass < 2; ++pass) {
        size_t pos = 0, written = 0;
        while (pos < value.length) {
            bool match = old_part.length <= value.length - pos
                         && !wmemcmp(value.data + pos, old_part.data, old_part.length);
            size_t count = match ? new_part.length : 1;
            if (count > GOAT_NATIVE_STRING_MAX - written)
                return GOAT_NATIVE_BAD_REQUEST;
            if (pass && count)
                wmemcpy(out->data + written, match ? new_part.data : value.data + pos, count);
            written += count;
            pos += match ? old_part.length : 1;
        }
        if (!pass) {
            length = written;
            uint32_t status = goat_native_string_storage(context, length, out);
            if (status != GOAT_NATIVE_OK)
                return status;
        }
    }
    return GOAT_NATIVE_OK;
}

/** @brief Formats %0, %1, ... using already converted argument views.
 * Missing or overflowing indices remain literal, as in the VM. Conversion of arbitrary
 * language values to strings belongs to the caller; this layer never touches VM objects.
 */
uint32_t goat_native_string_format(const goat_native_context_v2_t *context,
                                   goat_native_string_view_v2_t format,
                                   const goat_native_string_view_v2_t *arguments,
                                   size_t count,
                                   goat_native_string_buffer_v2_t *out) {
    if (count && !arguments)
        return GOAT_NATIVE_BAD_REQUEST;
    for (int pass = 0; pass < 2; ++pass) {
        size_t pos = 0, written = 0;
        while (pos < format.length) {
            size_t end = pos + 1;
            goat_native_string_view_v2_t part = {format.data + pos, 1};
            if (format.data[pos] == L'%' && end < format.length && format.data[end] >= L'0'
                && format.data[end] <= L'9') {
                size_t index = 0;
                bool overflow = false;
                while (end < format.length && format.data[end] >= L'0'
                       && format.data[end] <= L'9') {
                    size_t digit = (size_t)(format.data[end++] - L'0');
                    if (index > (SIZE_MAX - digit) / 10)
                        overflow = true;
                    else if (!overflow)
                        index = index * 10 + digit;
                }
                part.length = end - pos;
                if (!overflow && index < count)
                    part = arguments[index];
            }
            if (part.length > GOAT_NATIVE_STRING_MAX - written)
                return GOAT_NATIVE_BAD_REQUEST;
            if (pass && part.length)
                wmemcpy(out->data + written, part.data, part.length);
            written += part.length;
            pos = end;
        }
        if (!pass) {
            uint32_t status = goat_native_string_storage(context, written, out);
            if (status != GOAT_NATIVE_OK)
                return status;
        }
    }
    return GOAT_NATIVE_OK;
}
