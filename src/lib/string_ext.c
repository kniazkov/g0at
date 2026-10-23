/**
 * @file string_ext.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#include "string_ext.h"

#include "allocate.h"
#include "goat/unicode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_STRING_BUILDER_CAPACITY 16

goat_char_t *WSTRDUP(const goat_char_t *wstr) {
    if (wstr == NULL) {
        return (goat_char_t *)CALLOC(sizeof(goat_char_t));
    }

    size_t len = goat_strlen(wstr);
    size_t mem_len = (len + 1) * sizeof(goat_char_t);
    goat_char_t *new_str = (goat_char_t *)ALLOC(mem_len);
    memcpy(new_str, wstr, mem_len);
    return new_str;
}

int string_comparator(const void *first, const void *second) {
    return goat_strcmp((goat_char_t *)first, (goat_char_t *)second);
}

void init_string_builder(string_builder_t *builder, size_t capacity) {
    if (capacity > SIZE_MAX / sizeof(goat_char_t) - 1)
        abort();
    if (capacity > 0) {
        if (capacity < INITIAL_STRING_BUILDER_CAPACITY) {
            capacity = INITIAL_STRING_BUILDER_CAPACITY;
        }
        builder->data = (goat_char_t *)ALLOC(sizeof(goat_char_t) * (capacity + 1));
        builder->data[0] = 0;
    } else {
        builder->data = NULL;
    }
    builder->length = 0;
    builder->capacity = capacity;
}

void resize_string_builder(string_builder_t *builder, size_t new_capacity) {
    if (builder->capacity >= new_capacity) {
        return;
    }
    const size_t maximum = SIZE_MAX / sizeof(goat_char_t) - 1;
    if (new_capacity > maximum) {
        fprintf(stderr, "\nString capacity overflow.\n");
        exit(EXIT_FAILURE);
    }
    size_t capacity = builder->capacity;
    if (capacity < INITIAL_STRING_BUILDER_CAPACITY) {
        capacity = INITIAL_STRING_BUILDER_CAPACITY;
    }
    while (capacity < new_capacity) {
        size_t increment = capacity / 2;
        capacity = increment > maximum - capacity ? maximum : capacity + increment;
    }
    goat_char_t *new_data = ALLOC((capacity + 1) * sizeof(goat_char_t));
    if (builder->data) {
        memcpy(new_data, builder->data, (builder->length + 1) * sizeof(goat_char_t));
        FREE(builder->data);
    } else {
        new_data[0] = 0;
    }
    builder->data = new_data;
    builder->capacity = capacity;
}

string_value_t append_char(string_builder_t *builder, goat_char_t symbol) {
    if (builder->length == builder->capacity) {
        resize_string_builder(builder, builder->length + 1);
    }
    builder->data[builder->length++] = symbol;
    builder->data[builder->length] = 0;
    return (string_value_t){builder->data, builder->length, true};
}

string_value_t
append_substring(string_builder_t *builder, const goat_char_t *wstr, size_t wstr_length) {
    if (wstr_length != 0) {
        if (wstr_length > SIZE_MAX / sizeof(goat_char_t) - 1 - builder->length)
            abort();
        size_t new_length = builder->length + wstr_length;
        uintptr_t source = (uintptr_t)wstr, base = (uintptr_t)builder->data;
        bool alias = builder->data && source >= base
                     && source - base <= builder->length * sizeof(goat_char_t);
        size_t offset = alias ? (source - base) / sizeof(goat_char_t) : 0;
        if (new_length > builder->capacity)
            resize_string_builder(builder, new_length);
        if (alias)
            wstr = builder->data + offset;
        memmove(builder->data + builder->length, wstr, wstr_length * sizeof(goat_char_t));
        builder->length += wstr_length;
        builder->data[builder->length] = 0;
    }
    return (string_value_t){builder->data, builder->length, builder->data != NULL};
}

string_value_t append_string_value(string_builder_t *builder, string_value_t value) {
    return append_substring(builder, value.data, value.length);
}

string_value_t append_string_view(string_builder_t *builder, string_view_t view) {
    return append_substring(builder, view.data, view.length);
}

string_value_t append_string(string_builder_t *builder, const goat_char_t *wstr) {
    return append_substring(builder, wstr, goat_strlen(wstr));
}

string_value_t append_ascii_string(string_builder_t *builder, const char *str) {
    size_t str_length = strlen(str);
    if (str_length != 0) {
        if (str_length > SIZE_MAX / sizeof(goat_char_t) - 1 - builder->length)
            abort();
        size_t new_length = builder->length + str_length;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        goat_char_t *dst = builder->data + builder->length;
        const char *src = str;
        while (*src) {
            *dst++ = (goat_char_t)*src++;
        }
        builder->length += str_length;
        builder->data[builder->length] = 0;
    }
    return (string_value_t){builder->data, builder->length, builder->data != NULL};
}

string_value_t append_repeated_char(string_builder_t *builder, goat_char_t symbol, size_t count) {
    if (count > 0) {
        if (count > SIZE_MAX / sizeof(goat_char_t) - 1 - builder->length)
            abort();
        size_t new_length = builder->length + count;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        for (size_t index = 0; index < count; index++) {
            builder->data[builder->length++] = symbol;
        }
        builder->data[builder->length] = 0;
    }
    return (string_value_t){builder->data, builder->length, builder->data != NULL};
}

/**
 * @brief Encodes one already validated Unicode scalar into one to four UTF-8 bytes.
 */
static int encode_utf8_char(uint32_t w, char *c) {
    if (w < 0x80) {
        c[0] = (char)w;
        return 1;
    }
    if (w < 0x800) {
        c[0] = (char)(0xC0 | (w >> 6));
        c[1] = (char)(0x80 | (w & 0x3F));
        return 2;
    }
    if (w < 0x10000) {
        c[0] = (char)(0xE0 | (w >> 12));
        c[1] = (char)(0x80 | ((w >> 6) & 0x3F));
        c[2] = (char)(0x80 | (w & 0x3F));
        return 3;
    }
    c[0] = (char)(0xF0 | (w >> 18));
    c[1] = (char)(0x80 | ((w >> 12) & 0x3F));
    c[2] = (char)(0x80 | ((w >> 6) & 0x3F));
    c[3] = (char)(0x80 | (w & 0x3F));
    return 4;
}

char *encode_utf8_view(string_view_t input, size_t *size_ptr) {
    if (size_ptr)
        *size_ptr = 0;
    if (!input.data)
        return NULL;
    const goat_char_t *wstr = input.data;
    size_t remaining = input.length;
    const size_t init_capacity = 16;
    char *buffer = ALLOC(init_capacity);
    size_t size = 0;
    size_t capacity = init_capacity;
    char symbol[4];
    while (remaining--) {
        uint32_t scalar = (uint32_t)*wstr++;
        if (!goat_is_scalar(scalar)) {
            FREE(buffer);
            return NULL;
        }
        int bytes_count = encode_utf8_char(scalar, symbol);
        if (size > SIZE_MAX - (size_t)bytes_count - 1) {
            fprintf(stderr, "\nUTF-8 size overflow.\n");
            exit(EXIT_FAILURE);
        }
        if (size + bytes_count + 1 > capacity) {
            capacity = capacity > SIZE_MAX / 2 ? SIZE_MAX : capacity * 2;
            char *new_buffer = ALLOC(capacity);
            memcpy(new_buffer, buffer, size);
            FREE(buffer);
            buffer = new_buffer;
        }
        memcpy(buffer + size, symbol, (size_t)bytes_count);
        size += bytes_count;
    }
    buffer[size] = '\0';
    if (size_ptr != NULL) {
        *size_ptr = size;
    }
    return buffer;
}

char *encode_utf8_ex(const goat_char_t *wstr, size_t *size_ptr) {
    return encode_utf8_view((string_view_t){wstr, goat_strlen(wstr)}, size_ptr);
}

char *encode_utf8(const goat_char_t *wstr) {
    return encode_utf8_ex(wstr, NULL);
}

string_value_t decode_utf8_n(const char *str, size_t length) {
    if (!str)
        return NULL_STRING_VALUE;
    const char *end = str + length;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = EMPTY_STRING_VALUE;
    while (str < end) {
        unsigned char first = (unsigned char)*str++;
        uint32_t scalar;
        uint32_t minimum;
        unsigned int remaining;
        if (first < 0x80) {
            scalar = first;
            minimum = 0;
            remaining = 0;
        } else if (first >= 0xC2 && first <= 0xDF) {
            scalar = first & 0x1F;
            minimum = 0x80;
            remaining = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            scalar = first & 0x0F;
            minimum = 0x800;
            remaining = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            scalar = first & 0x07;
            minimum = 0x10000;
            remaining = 3;
        } else {
            goto error;
        }
        for (unsigned int i = 0; i < remaining; i++) {
            if (str == end)
                goto error;
            unsigned char next = (unsigned char)*str;
            if ((next & 0xC0) != 0x80) {
                goto error;
            }
            str++;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        /* Reject overlong encodings, surrogate code points, and values above Unicode. */
        if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
            goto error;
        }
        { result = append_char(&builder, (goat_char_t)scalar); }
    }
    return result;

error:
    FREE(builder.data);
    return NULL_STRING_VALUE;
}

string_value_t decode_utf8(const char *str) {
    return decode_utf8_n(str, strlen(str));
}

string_value_t string_to_string_notation(const goat_char_t *prefix, const string_value_t str) {
    string_builder_t builder;
    init_string_builder(&builder, str.length + 2 + goat_strlen(prefix));
    append_string(&builder, prefix);
    append_char(&builder, '"');
    for (size_t index = 0; index < str.length; index++) {
        goat_char_t ch = str.data[index];
        switch (ch) {
            case 0:
                append_string(&builder, U"\\0");
                break;
            case '\r':
                append_substring(&builder, U"\\r", 2);
                break;
            case '\n':
                append_substring(&builder, U"\\n", 2);
                break;
            case '\t':
                append_substring(&builder, U"\\t", 2);
                break;
            case '"':
                append_substring(&builder, U"\\\"", 2);
                break;
            case '\\':
                append_substring(&builder, U"\\\\", 2);
                break;
            default:
                append_char(&builder, ch);
        }
    }
    return append_char(&builder, '"');
}

string_value_t format_string_view_vargs(string_view_t input, va_list args) {
    if (!input.data)
        return NULL_STRING_VALUE;
    const goat_char_t *format = input.data;
    const goat_char_t *end = format + input.length;
    const goat_char_t *ch = format;
    size_t size = 0;
    while (ch < end && *ch != U'%') {
        size++;
        ch++;
    }
    if (ch == end) {
        // no control symbols
        return (string_value_t){(goat_char_t *)format, size, false};
    }
    string_builder_t builder;
    init_string_builder(&builder,
                        size < INITIAL_STRING_BUILDER_CAPACITY ? INITIAL_STRING_BUILDER_CAPACITY
                                                               : size);
    append_substring(&builder, format, size);
    while (ch < end) {
        if (*ch == U'%') {
            if (++ch == end) {
                append_char(&builder, U'%');
                break;
            }
            switch (*ch) {
                case U'%':
                    append_char(&builder, U'%');
                    break;
                case U'c': {
                    goat_char_t arg_char = (goat_char_t)va_arg(args, int);
                    append_char(&builder, arg_char);
                    break;
                }
                case U'v': {
                    string_view_t view = va_arg(args, string_view_t);
                    append_string_view(&builder, view);
                    break;
                }
                case U's': {
                    const goat_char_t *arg_str = va_arg(args, const goat_char_t *);
                    append_string(&builder, arg_str);
                    break;
                }
                case U'a': {
                    const char *arg_str = va_arg(args, const char *);
                    append_ascii_string(&builder, arg_str);
                    break;
                }
                case U'd':
                case U'i': {
                    int arg_int = va_arg(args, int);
                    char buffer[16];
                    sprintf(buffer, "%d", arg_int);
                    append_ascii_string(&builder, buffer);
                    break;
                }
                case U'u': {
                    unsigned int arg_uint = va_arg(args, unsigned int);
                    char buffer[16];
                    sprintf(buffer, "%u", arg_uint);
                    append_ascii_string(&builder, buffer);
                    break;
                }
                case U'l': {
                    if (++ch == end) {
                        append_char(&builder, U'?');
                        break;
                    }
                    if (*ch == 'i' || *ch == 'd') {
                        int64_t arg_long = va_arg(args, int64_t);
                        char buffer[32];
                        sprintf(buffer, "%" PRId64, arg_long);
                        append_ascii_string(&builder, buffer);
                    } else {
                        append_char(&builder, U'?');
                    }
                    break;
                }
                case U'z': {
                    if (++ch == end) {
                        append_char(&builder, U'?');
                        break;
                    }
                    if (*ch == U'u') {
                        size_t arg_size = va_arg(args, size_t);
                        char buffer[32];
                        sprintf(buffer, "%zu", arg_size);
                        append_ascii_string(&builder, buffer);
                    } else {
                        append_char(&builder, U'?');
                    }
                    break;
                }
                case U'f': {
                    double arg_double = va_arg(args, double);
                    char buffer[32];
                    double_to_string(arg_double, buffer, sizeof(buffer));
                    append_ascii_string(&builder, buffer);
                    break;
                }
                default:
                    append_char(&builder, U'?');
                    break;
            }
        } else {
            append_char(&builder, *ch);
        }
        if (ch < end)
            ch++;
    }
    return (string_value_t){builder.data, builder.length, true};
}

string_value_t align_text(string_value_t text, size_t size, alignment_t alignment) {
    if (text.data == NULL || text.length == 0) {
        return EMPTY_STRING_VALUE;
    }
    if (size > SIZE_MAX / sizeof(goat_char_t) - 1)
        abort();
    goat_char_t *buff = ALLOC((size + 1) * sizeof(goat_char_t));
    if (text.length > size) {
        memcpy(buff, text.data, sizeof(goat_char_t) * size);
    } else {
        size_t offset;
        switch (alignment) {
            case ALIGN_CENTER:
                offset = (size - text.length) / 2;
                break;
            case ALIGN_RIGHT:
                offset = size - text.length;
                break;
            default:
                offset = 0;
        }
        size_t dst_index;
        for (dst_index = 0; dst_index < offset; dst_index++) {
            buff[dst_index] = U' ';
        }
        for (size_t src_index = 0; src_index < text.length; src_index++, dst_index++) {
            buff[dst_index] = text.data[src_index];
        }
        for (; dst_index < size; dst_index++) {
            buff[dst_index] = U' ';
        }
    }
    buff[size] = U'\0';
    return (string_value_t){buff, size, true};
}

void init_unicode_locale(void) {
}

goat_char_t unicode_towupper(goat_char_t ch) {
    return goat_simple_upper(ch);
}

goat_char_t unicode_towlower(goat_char_t ch) {
    return goat_simple_lower(ch);
}

string_value_t unicode_string_case(string_view_t value, bool upper) {
    if (!value.data)
        return NULL_STRING_VALUE;
    string_builder_t builder;
    init_string_builder(&builder, value.length);
    for (size_t i = 0; i < value.length; ++i) {
        goat_char_t mapped[3];
        size_t n = goat_case_mapping(value.data, value.length, i, upper, mapped);
        append_substring(&builder, mapped, n);
    }
    return builder.data ? (string_value_t){builder.data, builder.length, true} : EMPTY_STRING_VALUE;
}

int goat_snprintf(goat_char_t *out, size_t size, const goat_char_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    size_t n = size && value.length >= size ? size - 1 : value.length;
    if (size) {
        goat_memcpy(out, value.data, n);
        out[n] = 0;
    }
    int result = (int)value.length;
    FREE_STRING(value);
    return result;
}

double goat_strtod(const goat_char_t *input, goat_char_t **end) {
    size_t n = 0;
    while (input[n] && input[n] < 128
           && (goat_is_digit(input[n]) || goat_strchr(U".+-eE", input[n])))
        ++n;
    char *ascii = ALLOC(n + 1);
    for (size_t i = 0; i < n; ++i)
        ascii[i] = (char)input[i];
    ascii[n] = 0;
    char *tail;
    double result = parse_ascii_real(ascii, &tail);
    if (end)
        *end = (goat_char_t *)input + (tail - ascii);
    FREE(ascii);
    return result;
}

uint64_t string_view_hash(string_view_t value) {
    if (!value.data)
        return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < value.length; ++i)
        for (unsigned j = 0; j < 4; ++j) {
            hash ^= (uint8_t)(value.data[i] >> (8 * j));
            hash *= UINT64_C(1099511628211);
        }
    return hash;
}

char *encode_utf8_path(string_view_t path) {
    if (!path.data || goat_memchr(path.data, 0, path.length))
        return NULL;
    return encode_utf8_view(path, NULL);
}

string_value_t format_string_vargs(const goat_char_t *format, va_list args) {
    return format_string_view_vargs((string_view_t){format, goat_strlen(format)}, args);
}

string_value_t format_string_view(string_view_t format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t result = format_string_view_vargs(format, args);
    va_end(args);
    return result;
}

string_value_t decode_utf16(const uint16_t *input, size_t length) {
    if (!input)
        return NULL_STRING_VALUE;
    string_builder_t builder;
    init_string_builder(&builder, length);
    for (size_t i = 0; i < length; ++i) {
        uint32_t scalar = input[i];
        if (scalar >= 0xd800 && scalar <= 0xdbff) {
            if (++i == length || input[i] < 0xdc00 || input[i] > 0xdfff)
                goto invalid;
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + input[i] - 0xdc00;
        }
        if (!goat_is_scalar(scalar))
            goto invalid;
        append_char(&builder, scalar);
    }
    return builder.data ? (string_value_t){builder.data, builder.length, true} : EMPTY_STRING_VALUE;
invalid:
    FREE(builder.data);
    return NULL_STRING_VALUE;
}

uint16_t *encode_utf16(string_view_t input, size_t *length) {
    if (length)
        *length = 0;
    if (!input.data)
        return NULL;
    if (input.length > (SIZE_MAX / sizeof(uint16_t) - 1) / 2)
        abort();
    uint16_t *result = ALLOC((2 * input.length + 1) * sizeof(uint16_t));
    size_t n = 0;
    for (size_t i = 0; i < input.length; ++i) {
        uint32_t scalar = input.data[i];
        if (!goat_is_scalar(scalar)) {
            FREE(result);
            return NULL;
        }
        if (scalar < 0x10000)
            result[n++] = (uint16_t)scalar;
        else {
            scalar -= 0x10000;
            result[n++] = (uint16_t)(0xd800 + (scalar >> 10));
            result[n++] = (uint16_t)(0xdc00 + (scalar & 0x3ff));
        }
    }
    result[n] = 0;
    if (length)
        *length = n;
    return result;
}
