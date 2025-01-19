/**
 * @file string_ext.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

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
        builder->data = (wchar_t *)ALLOC(sizeof(wchar_t) * (capacity + 1));
        builder->data[0] = 0;
    } else {
        builder->data = NULL;
    }
    builder->length = 0;
    builder->capacity = capacity;
}

void resize_string_builder(string_builder_t *builder, size_t new_capacity) {
    if (builder->capacity > new_capacity) {
        return;
    }
    wchar_t *new_data = ALLOC((new_capacity + 1) * sizeof(wchar_t));
    if (builder->data) {
        memcpy(new_data, builder->data, (builder->length + 1) * sizeof(wchar_t));
        FREE(builder->data);
    }
    builder->data = new_data;
    builder->capacity = new_capacity;
}

string_value_t append_char(string_builder_t *builder, wchar_t symbol) {
    if (builder->length == builder->capacity) {
        resize_string_builder(builder,
            builder->capacity > 0 ? builder->capacity * 3 / 2 : INITIAL_STRING_BUILDER_CAPACITY);
    }
    builder->data[builder->length++] = symbol;
    builder->data[builder->length] = 0;
    return (string_value_t){ builder->data, builder->length, true };
}

string_value_t append_substring(string_builder_t *builder, const wchar_t *wstr, size_t wstr_length) {
    if (wstr_length != 0) {
        size_t new_length = builder->length + wstr_length;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        memcpy(builder->data + builder->length, wstr, (wstr_length + 1) * sizeof(wchar_t));
        builder->length += wstr_length;
    }
    return (string_value_t){ builder->data, builder->length, true };
}

string_value_t append_string(string_builder_t *builder, const wchar_t *wstr) {
    return append_substring(builder, wstr, wcslen(wstr));
}

/**
 * @brief Encodes a single wide-character symbol (`wchar_t`) into UTF-8.
 * 
 * This function encodes a single wide-character symbol (`wchar_t`) into its UTF-8 representation.
 * It supports the full range of Unicode characters, from 1-byte characters (ASCII) to
 * 4-byte characters (for Unicode code points beyond the Basic Multilingual Plane).
 * 
 * The function returns the number of bytes written to the `c` array (1, 2, 3, or 4),
 * depending on the Unicode code point, and stores the UTF-8 encoded bytes in the provided buffer.
 * 
 * @param w The wide-character symbol (`wchar_t`) to encode in UTF-8.
 * @param c A buffer to hold the resulting UTF-8 encoded bytes. The buffer should be large enough
 *  to hold up to 4 bytes (depending on the character).
 * @return The number of bytes written to `c`, or 0 if the character cannot be encoded.
 */
static int encode_utf8_char(wchar_t w, char *c) {
    if (w < 0x80) {
        c[0] = (char)w;
        return 1;
    }
    if (w < 0x800) {
        c[0] = (char)((w & 0x7C0) >> 6) + 0xC0;
        c[1] = (char)(w & 0x3F) + 0x80;
        return 2;
    }
    if (w < 0x10000) {
        c[0] = (char)((w & 0xF000) >> 12) + 0xE0;
        c[1] = (char)((w & 0xFC0) >> 6) + 0x80;
        c[2] = (char)(w & 0x3F) + 0x80;
        return 3;
    }
#if WCHAR_MAX > 0xFFFF
    if (w < 0x200000) {
        c[0] = (char)((w & 0x1C0000) >> 18) + 0xF0;
        c[1] = (char)((w & 0x3F000) >> 12) + 0x80;
        c[2] = (char)((w & 0xFC0) >> 6) + 0x80;
        c[3] = (char)(w & 0x3F) + 0x80;
        return 3;
    }
#endif
    return 0;
}

char *encode_utf8_ex(const wchar_t *wstr, size_t *size_ptr) {
    const size_t init_capacity = 16;
    char *buffer = ALLOC(init_capacity);
    size_t size = 0;
    size_t capacity = init_capacity;
    char symbol[4];
    while (*wstr) {
        int bytes_count = encode_utf8_char(*wstr, symbol);
        if (bytes_count > 0) {
            if (size + bytes_count + 1 > capacity) {
                capacity *= 2;
                char *new_buffer = ALLOC(capacity);
                memcpy(new_buffer, buffer, size);
                FREE(buffer);
                buffer = new_buffer;
            }
            for (int i = 0; i < bytes_count; i++) {
                buffer[size++] = symbol[i];
            }
        }
        wstr++;
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
    init_string_builder(&builder, 16);
    string_value_t result = { L"", 0, false };
    while (*str) {
        unsigned char c0 = (unsigned char)*str++;
        wchar_t w = 0;
        if ((c0 & 0x80) == 0) {
            w = c0;
        }
        else if ((c0 & 0xE0) == 0xC0) {
            if (*str == '\0') {
                goto error;
            }
            unsigned char c1 = (unsigned char)*str++;
            if ((c1 & 0xC0) != 0x80) {
                goto error;
            }
            w = ((c0 & 0x1F) << 6) + (c1 & 0x3F);
        }
        else if ((c0 & 0xF0) == 0xE0) {
            if (*str == '\0') {
                goto error;
            }
            unsigned char c1 = (unsigned char)*str++;
            if (*str == '\0') {
                goto error;
            }
            unsigned char c2 = (unsigned char)*str++;
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
                goto error;
            }
            w = ((c0 & 0xF) << 12) + ((c1 & 0x3F) << 6) + (c2 & 0x3F);
        }
#if WCHAR_MAX > 0xFFFF
        else if ((c0 & 0xF8) == 0xF0) {
            if (*str == '\0') {
                goto error;
            }
            unsigned char c1 = (unsigned char)*str++;
            if (*str == '\0') {
                goto error;
            }
            unsigned char c2 = (unsigned char)*str++;
            if (*str == '\0') {
                goto error;
            }
            unsigned char c3 = (unsigned char)*str++;
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                goto error;
            }
            w = ((c0 & 0x7) << 18) + ((c1 & 0x3F) << 12) + ((c2 & 0x3F) << 6) + (c3 & 0x3F);
        }
#endif
        else {
            goto error;
        }
        result = append_char(&builder, w);
    }
    return result;

error:
    FREE(builder.data);
    return (string_value_t){ NULL, 0, false };
}

string_value_t string_to_string_notation(const wchar_t *prefix, const string_value_t str) {
    string_builder_t builder;
    init_string_builder(&builder, str.length + 2 + wcslen(prefix));
    append_string(&builder, prefix);
    append_char(&builder, '"');
    for (size_t i = 0; i < str.length; i++) {
        wchar_t ch = str.data[i];
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
