/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Generated string adapter validation and borrowed argument/result lifetimes.
 */
#include "goat/native_strings.h"
#include "lib/allocate.h"
#include "model/native_services.h"

#include <assert.h>
#include <string.h>

/** @brief The added pointer payload must preserve the previous numeric ABI layout. */
typedef struct {
    uint32_t type, reserved;

    union {
        int64_t integer;
        double real;
    } value;
} numeric_value_t;

_Static_assert(sizeof(numeric_value_t) == sizeof(goat_native_value_v2_t), "numeric ABI size");
_Static_assert(_Alignof(numeric_value_t) == _Alignof(goat_native_value_v2_t),
               "numeric ABI alignment");

int main(void) {
    const goat_native_module_v2_t *module = goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    assert(module && module->entry_count == 1);
    const goat_native_entry_v2_t *entry = module->entries;
    assert(entry->parameter_count == 1 && entry->parameter_types[0] == GOAT_NATIVE_STRING);
    assert(entry->return_type == GOAT_NATIVE_STRING && entry->flags == GOAT_NATIVE_PURE);
    native_memory_t memory = {0};
    goat_native_context_v2_t context = init_native_context(&memory);
    size_t baseline = get_allocated_memory_size();
    const goat_char_t data[] = {U'a', 0, U'b'}; /* A view need not be terminated. */
    goat_native_string_view_v2_t view = {data, 3};
    goat_native_value_v2_t argument = {.type = GOAT_NATIVE_STRING, .value.string = &view};
    goat_native_value_v2_t result = {.type = GOAT_NATIVE_I64, .value.integer = 123};
    assert(entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result)
           == GOAT_NATIVE_OK);
    assert(result.type == GOAT_NATIVE_STRING && result.reserved == 0);
    assert(result.value.string->data == data && result.value.string->length == 3);
    assert(get_allocated_memory_size() > baseline);
    assert(!goat_memcmp(result.value.string->data, data, 3));
    goat_native_deallocate_all(&context);
    assert(get_allocated_memory_size() == baseline);
    view = (goat_native_string_view_v2_t){0};
    assert(entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result)
           == GOAT_NATIVE_OK);
    assert(result.value.string && !result.value.string->length && !result.value.string->data);
    goat_native_deallocate_all(&context);
    for (int mode = 0; mode < 6; ++mode) {
        argument = (goat_native_value_v2_t){.type = GOAT_NATIVE_STRING, .value.string = &view};
        view = (goat_native_string_view_v2_t){data, 3};
        uint32_t expected = GOAT_NATIVE_BAD_REQUEST;
        if (mode == 0)
            argument.value.string = NULL;
        if (mode == 1)
            view.data = NULL;
        if (mode == 2)
            view.length = SIZE_MAX;
        if (mode == 3)
            argument.reserved = 1;
        if (mode == 4) {
            argument.type = GOAT_NATIVE_I64;
            expected = GOAT_NATIVE_TYPE_MISMATCH;
        }
        if (mode == 5)
            expected = GOAT_NATIVE_TYPE_MISMATCH;
        result = (goat_native_value_v2_t){.type = GOAT_NATIVE_I64, .value.integer = 123};
        assert(
            entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, mode == 5 ? 0 : 1, &argument, &result)
            == expected);
        assert(result.type == GOAT_NATIVE_I64 && result.value.integer == 123);
        assert(get_allocated_memory_size() == baseline);
    }
    return 0;
}
