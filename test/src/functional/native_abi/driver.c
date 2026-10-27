/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Calls a separately compiled generated module through the public ABI only.
 */
#include "goat/native_abi.h"
#include "test_allocator.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static const goat_native_entry_v2_t *
find(const goat_native_module_v2_t *module, uint64_t function, uint32_t first) {
    for (uint32_t i = 0; i < module->entry_count; i++) {
        const goat_native_entry_v2_t *entry = &module->entries[i];
        if (entry->function_id == function
            && (!entry->parameter_count || entry->parameter_types[0] == first))
            return entry;
    }
    assert(0);
    return 0;
}

int test_native_abi(goat_native_query_v2_t query) {
    assert(!query(0));
    assert(!query(0));
    assert(!query(GOAT_NATIVE_ABI_VERSION + 1));
    const goat_native_module_v2_t *module = query(GOAT_NATIVE_ABI_VERSION);
    assert(module && module == query(GOAT_NATIVE_ABI_VERSION));
    assert(module->abi_version == GOAT_NATIVE_ABI_VERSION);
    assert(module->struct_size == sizeof(*module));
    assert(module->value_size == sizeof(goat_native_value_v2_t));
    assert(module->value_alignment == _Alignof(goat_native_value_v2_t));
    assert(module->entry_size == sizeof(goat_native_entry_v2_t));
    assert(module->pointer_size == sizeof(void *));
    assert(module->entry_count == 6 && module->entries);
    for (uint32_t i = 0; i < module->entry_count; i++) {
        assert(module->entries[i].invoke);
        assert(module->entries[i].flags == GOAT_NATIVE_PURE);
        assert(!module->entries[i].binding_name);
        for (uint32_t j = 0; j < i; j++)
            assert(module->entries[i].specialization_id != module->entries[j].specialization_id);
    }
    const goat_native_entry_v2_t *integer = find(module, 1, GOAT_NATIVE_I64);
    const goat_native_entry_v2_t *real = find(module, 1, GOAT_NATIVE_F64);
    const goat_native_entry_v2_t *mixed = find(module, 2, GOAT_NATIVE_I64);
    const goat_native_entry_v2_t *reverse = find(module, 2, GOAT_NATIVE_F64);
    const goat_native_entry_v2_t *constant = find(module, 3, 0);
    const goat_native_entry_v2_t *fib = find(module, 4, GOAT_NATIVE_I64);
    assert(integer->return_type == GOAT_NATIVE_I64 && real->return_type == GOAT_NATIVE_F64);
    assert(mixed->parameter_count == 2 && mixed->parameter_types[1] == GOAT_NATIVE_F64);
    assert(reverse->parameter_count == 2 && reverse->parameter_types[1] == GOAT_NATIVE_I64);
    assert(!constant->parameter_count && !constant->parameter_types);
    goat_native_value_v2_t untouched = {.type = 99};
    assert(constant->invoke(0, NULL, 0, NULL, &untouched) == GOAT_NATIVE_ABI_MISMATCH);
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, NULL, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    goat_native_context_v2_t allocator = test_context();
    goat_native_services_v2_t services = *allocator.services;
    allocator.services = &services;
    services.struct_size = 0;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    services.struct_size = sizeof(services);
    services.allocate = NULL;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    allocator.services = NULL;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    allocator = test_context();
    services = *allocator.services;
    allocator.services = &services;
    services.deallocate = NULL;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    services = *test_context().services;
    services.deallocate_all = NULL;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(untouched.type == 99);

    struct {
        goat_native_services_v2_t memory;
        void *future_service;
    } extended = {*test_context().services, NULL};

    extended.memory.struct_size = sizeof(extended);
    allocator.services = &extended.memory;
    assert(constant->invoke(GOAT_NATIVE_ABI_VERSION, &allocator, 0, NULL, &untouched)
               == GOAT_NATIVE_OK
           && untouched.value.integer == 42);

    goat_native_value_v2_t args[3] = {{.type = GOAT_NATIVE_I64}, {.type = GOAT_NATIVE_F64}, {0}};
    goat_native_value_v2_t result = {0};
    const int64_t integers[] = {INT64_MIN, INT64_MAX, 0, -1, INT64_C(9007199254740993)};
    for (unsigned i = 0; i < sizeof(integers) / sizeof(*integers); i++) {
        args[0].value.integer = integers[i];
        assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
               == GOAT_NATIVE_OK);
        assert(result.type == GOAT_NATIVE_I64 && !result.reserved
               && result.value.integer == integers[i]);
    }
    const double reals[] = {-0.0, 0.0, 0x0.0000000000001p-1022, INFINITY, -INFINITY, NAN, 1.25};
    for (unsigned i = 0; i < sizeof(reals) / sizeof(*reals); i++) {
        args[0].type = GOAT_NATIVE_F64;
        args[0].value.real = reals[i];
        assert(test_invoke(real->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
               == GOAT_NATIVE_OK);
        assert(result.type == GOAT_NATIVE_F64 && !result.reserved);
        if (isnan(reals[i]))
            assert(isnan(result.value.real));
        else
            assert(result.value.real == reals[i]
                   && !!signbit(result.value.real) == !!signbit(reals[i]));
    }
    args[0] = (goat_native_value_v2_t){.type = GOAT_NATIVE_I64, .value.integer = 5};
    args[1] = (goat_native_value_v2_t){.type = GOAT_NATIVE_F64, .value.real = 0.5};
    assert(test_invoke(mixed->invoke, GOAT_NATIVE_ABI_VERSION, 2, args, &result) == GOAT_NATIVE_OK
           && result.value.real == 4.5);
    goat_native_value_v2_t swapped[2] = {args[1], args[0]};
    assert(test_invoke(reverse->invoke, GOAT_NATIVE_ABI_VERSION, 2, swapped, &result)
               == GOAT_NATIVE_OK
           && result.value.real == -4.5);
    args[2].type = UINT32_MAX;
    args[2].reserved = UINT32_MAX;
    assert(test_invoke(mixed->invoke, GOAT_NATIVE_ABI_VERSION, 3, args, &result) == GOAT_NATIVE_OK
           && result.value.real == 4.5);
    assert(test_invoke(constant->invoke, GOAT_NATIVE_ABI_VERSION, 0, 0, &result) == GOAT_NATIVE_OK
           && result.value.integer == 42);
    assert(test_invoke(constant->invoke, GOAT_NATIVE_ABI_VERSION, 3, args, &result)
               == GOAT_NATIVE_OK
           && result.value.integer == 42);
    args[0].value.integer = 10;
    assert(test_invoke(fib->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result) == GOAT_NATIVE_OK
           && result.value.integer == 55);
    /* Result may alias an argument: no write occurs before all inputs are consumed. */
    assert(test_invoke(mixed->invoke, GOAT_NATIVE_ABI_VERSION, 2, args, args) == GOAT_NATIVE_OK);
    assert(args[0].type == GOAT_NATIVE_F64 && args[0].value.real == 9.5);

    memset(&result, 0x5a, sizeof(result));
    unsigned char before[sizeof(result)];
    memcpy(before, &result, sizeof(result));
    assert(test_invoke(integer->invoke, 0, 0, 0, &result) == GOAT_NATIVE_ABI_MISMATCH);
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 0, 0, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, 0, &result)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, 0)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    args[0].type = GOAT_NATIVE_INVALID;
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    args[0].type = UINT32_MAX;
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    args[0].type = GOAT_NATIVE_I64;
    args[0].reserved = 1;
    assert(test_invoke(integer->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
           == GOAT_NATIVE_BAD_REQUEST);
    args[0].reserved = 0;
    args[1].type = GOAT_NATIVE_I64;
    assert(test_invoke(mixed->invoke, GOAT_NATIVE_ABI_VERSION, 2, args, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    assert(test_invoke(mixed->invoke, GOAT_NATIVE_ABI_VERSION, 1, args, &result)
           == GOAT_NATIVE_TYPE_MISMATCH);
    assert(test_invoke(constant->invoke, GOAT_NATIVE_ABI_VERSION, 0, 0, 0)
           == GOAT_NATIVE_BAD_REQUEST);
    assert(!memcmp(before, &result, sizeof(result)));
    return 0;
}

#ifndef GOAT_ABI_NO_MAIN
int main(void) {
    return test_native_abi(goat_native_query_v2);
}
#endif
