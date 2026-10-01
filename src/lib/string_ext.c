/**
 * @file string_ext.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "string_ext.h"
#include "allocate.h"

#define INITIAL_STRING_BUILDER_CAPACITY 16

wchar_t *WSTRDUP(const wchar_t *wstr) {
    if (wstr == NULL) {
        return (wchar_t *)CALLOC(sizeof(wchar_t));
    }

    size_t len = wcslen(wstr);
    size_t mem_len = (len + 1) * sizeof(wchar_t);
    wchar_t *new_str = (wchar_t *)ALLOC(mem_len);
    memcpy(new_str, wstr, mem_len);
    return new_str;
}

int string_comparator(const void *first, const void *second) {
    return wcscmp((wchar_t *)first, (wchar_t *)second);
}

void init_string_builder(string_builder_t *builder, size_t capacity) {
    if (capacity > 0) {
        if (capacity < INITIAL_STRING_BUILDER_CAPACITY) {
            capacity = INITIAL_STRING_BUILDER_CAPACITY;
        }
        builder->data = (wchar_t *)ALLOC(sizeof(wchar_t) * (capacity + 1));
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
    const size_t maximum = SIZE_MAX / sizeof(wchar_t) - 1;
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
    wchar_t *new_data = ALLOC((capacity + 1) * sizeof(wchar_t));
    if (builder->data) {
        memcpy(new_data, builder->data, (builder->length + 1) * sizeof(wchar_t));
        FREE(builder->data);
    } else {
        new_data[0] = 0;
    }
    builder->data = new_data;
    builder->capacity = capacity;
}

string_value_t append_char(string_builder_t *builder, wchar_t symbol) {
    if (builder->length == builder->capacity) {
        resize_string_builder(builder, builder->length + 1);
    }
    builder->data[builder->length++] = symbol;
    builder->data[builder->length] = 0;
    return (string_value_t){ builder->data, builder->length, true };
}

string_value_t append_substring(string_builder_t *builder, const wchar_t *wstr,
        size_t wstr_length) {
    if (wstr_length != 0) {
        size_t new_length = builder->length + wstr_length;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        memcpy(builder->data + builder->length, wstr, (wstr_length + 1) * sizeof(wchar_t));
        builder->length += wstr_length;
        builder->data[builder->length] = 0;
    }
    return (string_value_t){ builder->data, builder->length, builder->data != NULL };
}

string_value_t append_string_value(string_builder_t *builder, string_value_t value) {
    return append_substring(builder, value.data, value.length);
}

string_value_t append_string_view(string_builder_t *builder, string_view_t view) {
    return append_substring(builder, view.data, view.length);
}

string_value_t append_string(string_builder_t *builder, const wchar_t *wstr) {
    return append_substring(builder, wstr, wcslen(wstr));
}

string_value_t append_ascii_string(string_builder_t *builder, const char *str) {
    size_t str_length = strlen(str);
    if (str_length != 0) {
        size_t new_length = builder->length + str_length;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        wchar_t *dst = builder->data + builder->length;
        const char *src = str;
        while (*src) {
            *dst++ = (wchar_t)*src++;
        }
        builder->length += str_length;
        builder->data[builder->length] = 0;
    }
    return (string_value_t){ builder->data, builder->length, builder->data != NULL };
}

string_value_t append_repeated_char(string_builder_t *builder, wchar_t symbol, size_t count) {
    if (count > 0) {
        size_t new_length = builder->length + count;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        for (size_t index = 0; index < count; index++) {
            builder->data[builder->length++] = symbol;
        }
        builder->data[builder->length] = 0;
    }
    return (string_value_t){ builder->data, builder->length, builder->data != NULL };
}

/**
 * @brief Encodes one Unicode scalar value into one to four UTF-8 bytes. Invalid scalar values are
 * replaced with U+FFFD, keeping the output valid UTF-8.
 */
static int encode_utf8_char(uint32_t w, char *c) {
    if (w > 0x10FFFF || (w >= 0xD800 && w <= 0xDFFF)) {
        w = 0xFFFD;
    }
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

char *encode_utf8_ex(const wchar_t *wstr, size_t *size_ptr) {
    const size_t init_capacity = 16;
    char *buffer = ALLOC(init_capacity);
    size_t size = 0;
    size_t capacity = init_capacity;
    char symbol[4];
    while (*wstr) {
        uint32_t scalar = (uint32_t)*wstr++;
#if WCHAR_MAX <= 0xFFFF
        /* Windows wchar_t stores UTF-16 code units, not complete scalar values. */
        if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            uint32_t low = (uint32_t)*wstr;
            if (low >= 0xDC00 && low <= 0xDFFF) {
                scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
                wstr++;
            }
        }
#endif
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

char *encode_utf8(const wchar_t *wstr) {
    return encode_utf8_ex(wstr, NULL);
}

string_value_t decode_utf8(const char *str) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = EMPTY_STRING_VALUE;
    while (*str) {
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
            unsigned char next = (unsigned char)*str;
            if ((next & 0xC0) != 0x80) {
                goto error;
            }
            str++;
            scalar = (scalar << 6) | (next & 0x3F);
        }
        /* Reject overlong encodings, surrogate code points, and values above Unicode. */
        if (scalar < minimum || scalar > 0x10FFFF ||
                (scalar >= 0xD800 && scalar <= 0xDFFF)) {
            goto error;
        }
#if WCHAR_MAX <= 0xFFFF
        if (scalar >= 0x10000) {
            scalar -= 0x10000;
            append_char(&builder, (wchar_t)(0xD800 + (scalar >> 10)));
            result = append_char(&builder, (wchar_t)(0xDC00 + (scalar & 0x3FF)));
        } else
#endif
        {
            result = append_char(&builder, (wchar_t)scalar);
        }
    }
    return result;

error:
    FREE(builder.data);
    return NULL_STRING_VALUE;
}

string_value_t string_to_string_notation(const wchar_t *prefix, const string_value_t str) {
    string_builder_t builder;
    init_string_builder(&builder, str.length + 2 + wcslen(prefix));
    append_string(&builder, prefix);
    append_char(&builder, '"');
    for (size_t index = 0; index < str.length; index++) {
        wchar_t ch = str.data[index];
        switch (ch) {
            case '\r':
                append_substring(&builder, L"\\r", 2);
                break;
            case '\n':
                append_substring(&builder, L"\\n", 2);
                break;
            case '\t':
                append_substring(&builder, L"\\t", 2);
                break;
            case '"':
                append_substring(&builder, L"\\\"", 2);
                break;
            case '\\':
                append_substring(&builder, L"\\\\", 2);
                break;
            default:
                append_char(&builder, ch);
        }
    }
    return append_char(&builder, '"');
}

void double_to_string(double value, char *buffer, size_t buffer_size) {
    if ((value >= 1e-10 && value <= 1e10) || (value <= -1e-10 && value >= -1e10)) {
        char temp_buffer[32];
        snprintf(temp_buffer, sizeof(temp_buffer), "%.11f", value);
        char *dot = strchr(temp_buffer, '.');
        if (dot) {
            char *end = dot + strlen(dot) - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            if (*end == '.') {
                *(end + 1) = '0';
            }
        }
        snprintf(buffer, buffer_size, "%s", temp_buffer);
    } else {
        snprintf(buffer, buffer_size, "%g", value);
    }
}

string_value_t format_string_vargs(const wchar_t *format, va_list args) {
    const wchar_t *ch = format;
    size_t size = 0;
    while (*ch != L'\0' && *ch != L'%') {
        size++;
        ch++;
    }
    if (*ch == L'\0') {
        // no control symbols
        return (string_value_t) { (wchar_t*)format, size, false };
    }
    string_builder_t builder;
    init_string_builder(&builder, size < INITIAL_STRING_BUILDER_CAPACITY ? INITIAL_STRING_BUILDER_CAPACITY : size);
    append_substring(&builder, format, size);
    while(*ch != L'\0') {
        if (*ch == L'%') {
            switch(*(++ch)) {
                case L'%' :
                    append_char(&builder, L'%');
                    break;
                case L'c': {
                    wchar_t arg_char = (wchar_t)va_arg(args, int);
                    append_char(&builder, arg_char);
                    break;
                }
                case L's': {
                    const wchar_t *arg_str = va_arg(args, const wchar_t *);
                    append_string(&builder, arg_str);
                    break;
                }
                case L'a': {
                    const char *arg_str = va_arg(args, const char *);
                    append_ascii_string(&builder, arg_str);
                    break;
                }
                case L'd':
                case L'i': {
                    int arg_int = va_arg(args, int);
                    char buffer[16];
                    sprintf(buffer, "%d", arg_int);
                    append_ascii_string(&builder, buffer);
                    break;
                }
                case L'u': {
                    unsigned int arg_uint = va_arg(args, unsigned int);
                    char buffer[16];
                    sprintf(buffer, "%u", arg_uint);
                    append_ascii_string(&builder, buffer);
                    break;
                }
                case L'l': {
                    ch++;
                    if (*ch == 'i' || *ch == 'd') {
                        int64_t arg_long = va_arg(args, int64_t);
                        char buffer[32];
                        sprintf(buffer, "%" PRId64, arg_long);
                        append_ascii_string(&builder, buffer);
                    } else {
                        append_char(&builder, L'?');
                    }
                    break;
                }
                case L'z': {
                    ch++;
                    if (*ch == L'u') {
                        size_t arg_size = va_arg(args, size_t);
                        char buffer[32];
                        sprintf(buffer, "%zu", arg_size);
                        append_ascii_string(&builder, buffer);
                    } else {
                        append_char(&builder, L'?');
                    }
                    break;
                }
                case L'f': {
                    double arg_double = va_arg(args, double);
                    char buffer[32];
                    double_to_string(arg_double, buffer, sizeof(buffer));
                    append_ascii_string(&builder, buffer);
                    break;
                }
                default:
                    append_char(&builder, L'?');
                    break;
            }
        } else {
            append_char(&builder, *ch);
        }
        ch++;
    }
    return (string_value_t) { builder.data, builder.length, true };
}

string_value_t align_text(string_value_t text, size_t size, alignment_t alignment) {
    if (text.data == NULL || text.length == 0) {
        return EMPTY_STRING_VALUE;
    }
    wchar_t *buff = ALLOC((size + 1) * sizeof(wchar_t));
    if (text.length > size) {
        memcpy(buff, text.data, sizeof(wchar_t) * size);
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
            buff[dst_index] = L' ';
        }
        for (size_t src_index = 0; src_index < text.length; src_index++, dst_index++) {
            buff[dst_index] = text.data[src_index];
        }
        for (; dst_index < size; dst_index++) {
            buff[dst_index] = L' ';
        }
    }
    buff[size] = L'\0';
    return (string_value_t){ buff, size, true };
}
