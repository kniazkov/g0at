/** @file context.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Allocator propagation through generated calls and stack-limit recovery.
 */
#include "instrumented.c"

#include <assert.h>
#include <goat/native_allocator.h>

static size_t allocations;

static void *GOAT_NATIVE_CALL counted(void *context, size_t size) {
    allocations++;
    return goat_native_allocate(context, size);
}

int main(void) {
    const goat_native_module_v2_t *m = goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    const goat_native_entry_v2_t *fib = NULL;
    for (uint32_t i = 0; i < m->entry_count; i++)
        if (m->entries[i].function_id == 4)
            fib = &m->entries[i];
    assert(fib);
    goat_native_allocator_state_t state = {0};
    goat_native_allocator_v2_t a = goat_native_allocator_init(&state);
    a.allocate = counted;
    goat_native_value_v2_t arg = {.type = GOAT_NATIVE_I64, .value.integer = 10};
    goat_native_value_v2_t result = {0};
    assert(fib->invoke(GOAT_NATIVE_ABI_VERSION, &a, 1, &arg, &result) == GOAT_NATIVE_OK);
    assert(result.value.integer == 55 && allocations > 10 && state.first);
    a.deallocate_all(a.context);
    assert(!state.first);
    allocations = 0;
    arg.value.integer = 10000;
    result.value.integer = -123;
    assert(fib->invoke(GOAT_NATIVE_ABI_VERSION, &a, 1, &arg, &result)
           == GOAT_NATIVE_RESOURCE_LIMIT);
    assert(result.value.integer == -123 && allocations > 1 && state.first);
    a.deallocate_all(a.context);
    assert(!state.first);
    return 0;
}
