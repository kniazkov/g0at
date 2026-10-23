/** @file str.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native primitive-to-string conversions using host-owned storage.
 */
#include "goat/native_strings.h"
#include "lib/number_format.h"

#include <inttypes.h>
#include <stdio.h>

/** @brief Copies bounded ASCII formatting into native wide storage. */
static uint32_t from_ascii(const goat_native_context_v2_t *context,
                           const char *text,
                           goat_native_string_buffer_v2_t *out) {
    gchar_t wide[64];
    size_t length = 0;
    while (text[length]) {
        wide[length] = (unsigned char)text[length];
        length++;
    }
    return goat_native_string_copy(context, (goat_native_string_view_v2_t){wide, length}, out);
}

uint32_t goat_native_str_i64(const goat_native_context_v2_t *context,
                             int64_t value,
                             goat_native_string_buffer_v2_t *out) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRId64, value);
    return from_ascii(context, text, out);
}

uint32_t goat_native_str_f64(const goat_native_context_v2_t *context,
                             double value,
                             goat_native_string_buffer_v2_t *out) {
    char text[32];
    double_to_string(value, text, sizeof(text));
    return from_ascii(context, text, out);
}

uint32_t goat_native_str_bool(const goat_native_context_v2_t *context,
                              bool value,
                              goat_native_string_buffer_v2_t *out) {
    return from_ascii(context, value ? "true" : "false", out);
}

uint32_t goat_native_str_char(const goat_native_context_v2_t *context,
                              uint32_t value,
                              goat_native_string_buffer_v2_t *out) {
    gchar_t text = value;
    return goat_native_string_copy(context, (goat_native_string_view_v2_t){&text, 1}, out);
}
