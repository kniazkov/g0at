/** @file host.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises a manual adapter without compiler or VM internals.
 */
#include <assert.h>
#include <goat/native_abi.h>
#include <string.h>

/* Test instrumentation exported by the vendor archive. */
extern unsigned vendor_call_count(void);

int main(void) {
    assert(!goat_native_query_v1(0));
    const goat_native_module_v1_t *module = goat_native_query_v1(GOAT_NATIVE_ABI_VERSION);
    assert(module && module->entry_count == 1);
    assert(module->struct_size == sizeof(*module));
    assert(module->value_size == sizeof(goat_native_value_v1_t));
    assert(module->value_alignment == _Alignof(goat_native_value_v1_t));
    assert(module->entry_size == sizeof(goat_native_entry_v1_t));
    assert(module->pointer_size == sizeof(void *));
    const goat_native_entry_v1_t *entry = &module->entries[0];
    assert(!strcmp(entry->binding_name, "device.read"));
    assert(!entry->flags);
    assert(entry->specialization_id == 17 && entry->function_id == 9);
    assert(entry->parameter_count == 1 && entry->parameter_types[0] == GOAT_NATIVE_I64);
    assert(entry->return_type == GOAT_NATIVE_F64);
    goat_native_value_v1_t args[2] = {
        {.type = GOAT_NATIVE_I64, .value.integer = 3},
        {.type = UINT32_MAX, .reserved = UINT32_MAX},
    };
    goat_native_value_v1_t result;
    memset(&result, 0x5a, sizeof(result));
    unsigned char before[sizeof(result)];
    memcpy(before, &result, sizeof(result));
    assert(entry->invoke(0, 1, args, &result) == GOAT_NATIVE_ABI_MISMATCH);
    assert(entry->invoke(1, 0, 0, &result) == GOAT_NATIVE_TYPE_MISMATCH);
    assert(entry->invoke(1, 1, 0, &result) == GOAT_NATIVE_BAD_REQUEST);
    assert(entry->invoke(1, 1, args, 0) == GOAT_NATIVE_BAD_REQUEST);
    args[0].type = GOAT_NATIVE_F64;
    assert(entry->invoke(1, 1, args, &result) == GOAT_NATIVE_TYPE_MISMATCH);
    args[0].type = GOAT_NATIVE_I64;
    args[0].reserved = 1;
    assert(entry->invoke(1, 1, args, &result) == GOAT_NATIVE_BAD_REQUEST);
    assert(!vendor_call_count() && !memcmp(before, &result, sizeof(result)));
    args[0].reserved = 0;
    args[0].value.integer = -1;
    assert(entry->invoke(1, 1, args, &result) == GOAT_NATIVE_EXTERNAL_ERROR);
    assert(vendor_call_count() == 1 && !memcmp(before, &result, sizeof(result)));
    args[0].value.integer = 3;
    assert(entry->invoke(1, 2, args, &result) == GOAT_NATIVE_OK);
    assert(result.type == GOAT_NATIVE_F64 && !result.reserved && result.value.real == 3.25);
    assert(vendor_call_count() == 2);
    assert(entry->invoke(1, 1, args, args) == GOAT_NATIVE_OK);
    assert(args[0].type == GOAT_NATIVE_F64 && args[0].value.real == 3.25);
    assert(vendor_call_count() == 3);
    return 0;
}
