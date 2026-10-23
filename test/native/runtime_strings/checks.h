/** @file checks.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native-only boundary cases, borrowed slices, errors and allocator ownership.
 */
#pragma once

static void boundaries(const goat_native_context_v2_t *context) {
    goat_native_string_view_v2_t empty = {0};
    goat_native_string_buffer_v2_t out = {0};
    uint32_t ch = 123;
    assert(goat_native_string_char_at(empty, 0, &ch) == GOAT_NATIVE_BAD_REQUEST);
    assert(ch == 123);
    assert(goat_native_string_char_at(V(U"x"), -1, &ch) == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_char_at(V(U"x"), INT64_MAX, &ch) == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_substring(context, V(U"abc"), -1, 2, &out)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_substring(context, V(U"abc"), 2, 1, &out) == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_substring(context, V(U"abc"), 0, INT64_MAX, &out)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_repeat(context, V(U"abc"), -1, &out) == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_repeat(context, V(U"abc"), INT64_MAX, &out)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(!out.data && !out.length && !out.capacity);
    /* Unrepresentable results must fail before dereferencing or allocating. */
    goat_native_string_view_v2_t huge = {NULL, GOAT_NATIVE_STRING_MAX};
    assert(goat_native_string_concat(context, huge, V(U"x"), &out) == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_replace(context, V(U"xx"), V(U"x"), huge, &out)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_format(context, V(U"%0%0"), &huge, 1, &out)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(goat_native_string_format(context, V(U"%0"), NULL, 1, &out) == GOAT_NATIVE_BAD_REQUEST);
    assert(!out.data);
    /* Zero-terminated data is not a precondition of views. Embedded zeros are data. */
    const gchar_t units[] = {U'a', 0, U'b'};
    goat_native_string_view_v2_t raw = {units, 3};
    assert(goat_native_string_copy(context, raw, &out) == GOAT_NATIVE_OK);
    assert(out.length == 3 && !MEMCMP(out.data, units, 3) && out.data[3] == 0);
    goat_native_string_view_v2_t slice = {out.data + 1, 2};
    goat_native_string_buffer_v2_t joined = {0};
    assert(goat_native_string_concat(context, slice, slice, &joined) == GOAT_NATIVE_OK);
    assert(joined.length == 4 && joined.data[0] == 0 && joined.data[1] == U'b'
           && joined.data[2] == 0 && joined.data[3] == U'b');
    goat_native_string_release(context, &out);
    goat_native_string_release(context, &out);
    assert(!out.data && !out.length && !out.capacity);
    goat_native_string_release(context, &joined);
    assert(goat_native_string_repeat(context, empty, INT64_MAX, &out) == GOAT_NATIVE_OK);
    assert(goat_native_string_concat(context, empty, empty, &out) == GOAT_NATIVE_OK);
    assert(!out.data);
    /* A memory-only table remains valid for numeric modules and non-locale operations. */
    goat_native_services_v2_t services = *context->services;
    services.struct_size = GOAT_NATIVE_MEMORY_SERVICES_SIZE;
    goat_native_context_v2_t old = {&services, context->state};
    bool blank = false;
    assert(goat_native_has_memory_services(&old));
    assert(goat_native_string_is_blank(&old, empty, &blank) == GOAT_NATIVE_ABI_MISMATCH);
    assert(!blank);
    assert(goat_native_string_trim(&old, V(U" x "), &out) == GOAT_NATIVE_ABI_MISMATCH);
    assert(goat_native_string_to_upper_case(&old, V(U"x"), &out) == GOAT_NATIVE_ABI_MISMATCH);
    services.struct_size = sizeof(services);
    services.string_to_lower = NULL;
    assert(goat_native_string_to_lower_case(&old, V(U"X"), &out) == GOAT_NATIVE_ABI_MISMATCH);
    assert(goat_native_string_copy(NULL, V(U"x"), &out) == GOAT_NATIVE_ABI_MISMATCH);
    assert(goat_native_str_i64(context, INT64_MIN, &out) == GOAT_NATIVE_OK);
    assert(out.length == 20 && !STRCMP(out.data, U"-9223372036854775808"));
    goat_native_string_release(context, &out);
    assert(goat_native_str_f64(NULL, 1.0, &out) == GOAT_NATIVE_ABI_MISMATCH);
    assert(!out.data);
    /* Leave one result live for the host's counter and bulk cleanup assertions. */
    assert(goat_native_string_copy(context, V(U"host-owned result"), &out) == GOAT_NATIVE_OK);
}
