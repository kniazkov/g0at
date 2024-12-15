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

void init_string_builder(string_builder_t *builder) {
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
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

wchar_t *append_char(string_builder_t *builder, wchar_t symbol) {
    if (builder->length == builder->capacity) {
        resize_string_builder(builder,
            builder->capacity > 0 ? builder->capacity * 3 / 2 : INITIAL_STRING_BUILDER_CAPACITY);
    }
    builder->data[builder->length++] = symbol;
    builder->data[builder->length] = 0;
    return builder->data;
}

wchar_t *append_string(string_builder_t *builder, wchar_t *wstr) {
    size_t str_length = wcslen(wstr);
    if (str_length != 0) {
        size_t new_length = builder->length + str_length;
        if (new_length > builder->capacity) {
            resize_string_builder(builder, new_length);
        }
        memcpy(builder->data + builder->length, wstr, (str_length + 1) * sizeof(wchar_t));
        builder->length += str_length;
    }
    return builder->data;
}
