/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact scalar ABI tags, canonical booleans and full-width characters.
 */
#include "goat/native_abi.h"
#include "lib/memory/allocate.h"
#include "model/native_services.h"

#include <assert.h>
#include <string.h>

typedef struct {
    uint32_t type, reserved;

    union {
        int64_t integer;
        double real;
    } value;
} numeric_value_t;

_Static_assert(sizeof(numeric_value_t) == sizeof(goat_native_value_v2_t), "ABI size unchanged");
_Static_assert(_Alignof(numeric_value_t) == _Alignof(goat_native_value_v2_t),
               "ABI alignment unchanged");

int main(void) {
    const goat_native_module_v2_t *module = goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    assert(module);
    native_memory_t memory = {0};
    goat_native_context_v2_t context = init_native_context(&memory);
    size_t baseline = get_allocated_memory_size();
    unsigned checked = 0;
    for (uint32_t i = 0; i < module->entry_count; i++) {
        const goat_native_entry_v2_t *entry = &module->entries[i];
        if (entry->function_id != 1 || entry->parameter_count != 1)
            continue;
        uint32_t type = entry->parameter_types[0];
        if (type != GOAT_NATIVE_BOOL && type != GOAT_NATIVE_CHAR)
            continue;
        checked++;
        assert(entry->return_type == type);
        const uint32_t values[] = {0, 1, 0xd800, 0x10ffff, UINT32_MAX};
        for (size_t j = 0; j < sizeof(values) / sizeof(*values); j++) {
            goat_native_value_v2_t argument = {.type = type};
            if (type == GOAT_NATIVE_BOOL)
                argument.value.boolean = values[j];
            else
                argument.value.character = values[j];
            goat_native_value_v2_t result = {.type = GOAT_NATIVE_I64,
                                             .reserved = 71,
                                             .value.integer = -123};
            goat_native_value_v2_t original = result;
            uint32_t status =
                entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result);
            if (type == GOAT_NATIVE_BOOL && values[j] > 1) {
                assert(status == GOAT_NATIVE_BAD_REQUEST);
                assert(!memcmp(&result, &original, sizeof(result)));
            } else {
                assert(status == GOAT_NATIVE_OK && result.type == type && !result.reserved);
                assert((type == GOAT_NATIVE_BOOL ? result.value.boolean : result.value.character)
                       == values[j]);
            }
            assert(get_allocated_memory_size() == baseline);
        }
        goat_native_value_v2_t argument = {.type = GOAT_NATIVE_I64, .value.integer = 1};
        goat_native_value_v2_t result = {.type = GOAT_NATIVE_I64, .value.integer = 123};
        assert(entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result)
               == GOAT_NATIVE_TYPE_MISMATCH);
        argument.type = type == GOAT_NATIVE_BOOL ? GOAT_NATIVE_CHAR : GOAT_NATIVE_BOOL;
        assert(entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result)
               == GOAT_NATIVE_TYPE_MISMATCH);
        argument.type = type;
        argument.reserved = 1;
        assert(entry->invoke(GOAT_NATIVE_ABI_VERSION, &context, 1, &argument, &result)
               == GOAT_NATIVE_BAD_REQUEST);
        assert(result.type == GOAT_NATIVE_I64 && result.value.integer == 123);
    }
    assert(checked == 2);
    goat_native_deallocate_all(&context);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
