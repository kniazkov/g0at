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

string_value_t append_substring(string_builder_t *builder, wchar_t *wstr, size_t wstr_length) {
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

string_value_t append_string(string_builder_t *builder, wchar_t *wstr) {
    return append_substring(builder, wstr, wcslen(wstr));
}
