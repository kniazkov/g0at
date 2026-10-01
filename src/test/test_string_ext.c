/**
 * @file test_string_ext.c
 * @copyright 2026 Ivan Kniazkov
 * @brief String growth and Unicode conversion regression tests.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "test_lib.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

bool test_string_geometric_growth() {
    for (int method = 0; method < 6; method++) {
        string_builder_t builder;
        init_string_builder(&builder, 0);
        string_value_t result = EMPTY_STRING_VALUE;
        size_t growths = 0;
        for (size_t i = 0; i < 4096; i++) {
            size_t previous = builder.capacity;
            switch (method) {
                case 0: result = append_char(&builder, L'x'); break;
                case 1: result = append_string(&builder, L"x"); break;
                case 2: result = append_ascii_string(&builder, "x"); break;
                case 3: result = append_repeated_char(&builder, L'x', 1); break;
                case 4: result = append_string_value(&builder, STATIC_STRING(L"x")); break;
                case 5: result = append_string_view(&builder, (string_view_t){L"x", 1}); break;
            }
            if (builder.capacity != previous) growths++;
            ASSERT(builder.capacity >= builder.length);
        }
        ASSERT(growths < 25);
        wchar_t *buffer = builder.data;
        size_t capacity = builder.capacity;
        resize_string_builder(&builder, capacity);
        ASSERT(builder.data == buffer && builder.capacity == capacity);
        resize_string_builder(&builder, capacity - 1);
        ASSERT(builder.data == buffer && builder.capacity == capacity);
        /* Consume only the final append result; the builder is now finished. */
        ASSERT(result.length == 4096 && result.should_free);
        for (size_t i = 0; i < result.length; i++) ASSERT(result.data[i] == L'x');
        ASSERT(result.data[result.length] == 0);
        FREE_STRING(result);
    }
    string_builder_t builder;
    init_string_builder(&builder, 0);
    resize_string_builder(&builder, 100);
    ASSERT(builder.capacity >= 100 && builder.length == 0 && builder.data[0] == 0);
    string_value_t result = append_repeated_char(&builder, L'z', 10000);
    ASSERT(result.length == 10000 && result.data[9999] == L'z' && result.data[10000] == 0);
    FREE_STRING(result);
    return true;
}

bool test_utf8_roundtrip() {
    const struct { uint32_t scalar; const char *bytes; } cases[] = {
        { 0x01, "\x01" }, { 0x7F, "\x7F" }, { 0x80, "\xC2\x80" },
        { 0x7FF, "\xDF\xBF" }, { 0x800, "\xE0\xA0\x80" },
        { 0xD7FF, "\xED\x9F\xBF" }, { 0xE000, "\xEE\x80\x80" },
        { 0xFFFF, "\xEF\xBF\xBF" }, { 0x10000, "\xF0\x90\x80\x80" },
        { 0x1F600, "\xF0\x9F\x98\x80" }, { 0x10FFFF, "\xF4\x8F\xBF\xBF" }
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        wchar_t wide[3] = {0};
        size_t units = 1;
#if WCHAR_MAX <= 0xFFFF
        if (cases[i].scalar >= 0x10000) {
            uint32_t scalar = cases[i].scalar - 0x10000;
            wide[0] = (wchar_t)(0xD800 + (scalar >> 10));
            wide[1] = (wchar_t)(0xDC00 + (scalar & 0x3FF));
            units = 2;
        } else
#endif
        wide[0] = (wchar_t)cases[i].scalar;
        size_t size;
        char *encoded = encode_utf8_ex(wide, &size);
        ASSERT(size == strlen(cases[i].bytes));
        ASSERT(strcmp(encoded, cases[i].bytes) == 0);
        FREE(encoded);
        string_value_t decoded = decode_utf8(cases[i].bytes);
        ASSERT(decoded.data && decoded.length == units);
        ASSERT(memcmp(decoded.data, wide, (units + 1) * sizeof(wchar_t)) == 0);
        FREE_STRING(decoded);
    }
    /* Cross multiple output-buffer growth boundaries with mixed UTF-8 widths. */
    const char *mixed = "ASCII \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 "
        "\xE4\xB8\xAD \xF0\x9F\x98\x80 \xF4\x8F\xBF\xBF end";
    string_value_t decoded = decode_utf8(mixed);
    ASSERT(decoded.data != NULL);
    char *encoded = encode_utf8(decoded.data);
    ASSERT(strcmp(encoded, mixed) == 0);
    FREE(encoded);
    FREE_STRING(decoded);
    size_t before = get_allocated_memory_size();
    decoded = decode_utf8("");
    ASSERT(decoded.data && decoded.length == 0);
    FREE_STRING(decoded);
    ASSERT(get_allocated_memory_size() == before);
    size_t size = 99;
    encoded = encode_utf8_ex(L"", &size);
    ASSERT(size == 0 && encoded[0] == 0);
    FREE(encoded);
    return true;
}

bool test_utf8_invalid_sequences() {
    const char *invalid[] = {
        "\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80",
        "\xF0\x80\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF",
        "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF",
        "\xC2", "\xE2", "\xE2\x82", "\xF0", "\xF0\x9F", "\xF0\x9F\x98",
        "\xC2" "A", "\xE2\x82" "A", "ok\xF0\x9F\x98", "ok\x80"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        size_t before = get_allocated_memory_size();
        string_value_t decoded = decode_utf8(invalid[i]);
        ASSERT(decoded.data == NULL && decoded.length == 0 && !decoded.should_free);
        ASSERT(get_allocated_memory_size() == before);
    }
    const wchar_t invalid_wide[] = { (wchar_t)0xD800, L'A', (wchar_t)0xDC00, 0 };
    char *encoded = encode_utf8(invalid_wide);
    ASSERT(strcmp(encoded, "\xEF\xBF\xBD" "A" "\xEF\xBF\xBD") == 0);
    FREE(encoded);
    const wchar_t trailing_high[] = { (wchar_t)0xD800, 0 };
    encoded = encode_utf8(trailing_high);
    ASSERT(strcmp(encoded, "\xEF\xBF\xBD") == 0);
    FREE(encoded);
#if WCHAR_MAX > 0xFFFF
    const wchar_t out_of_range[] = { (wchar_t)0x110000, (wchar_t)-1, 0 };
    encoded = encode_utf8(out_of_range);
    ASSERT(strcmp(encoded, "\xEF\xBF\xBD\xEF\xBF\xBD") == 0);
    FREE(encoded);
#endif
    return true;
}
