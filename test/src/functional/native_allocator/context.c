/** @file context.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Allocator propagation through generated calls and stack-limit recovery.
 */
#include "instrumented.c"
#include "lib/memory/allocate.h"
#include "model/native_services.h"

#include <assert.h>

static size_t allocations;
static void *(GOAT_NATIVE_CALL *host_allocate)(void *, size_t);

static void *GOAT_NATIVE_CALL counted(void *context, size_t size) {
    allocations++;
    return host_allocate(context, size);
}

int main(void) {
    const goat_native_module_v2_t *m = goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);
    const goat_native_entry_v2_t *fib = NULL;
    for (uint32_t i = 0; i < m->entry_count; i++)
        if (m->entries[i].function_id == 4)
            fib = &m->entries[i];
    assert(fib);
    native_memory_t state = {0};
    goat_native_context_v2_t a = init_native_context(&state);
    goat_native_services_v2_t services = *a.services;
    host_allocate = services.allocate;
    services.allocate = counted;
    a.services = &services;
    size_t baseline = get_allocated_memory_size();
    goat_native_value_v2_t arg = {.type = GOAT_NATIVE_I64, .value.integer = 10};
    goat_native_value_v2_t result = {0};
    assert(fib->invoke(GOAT_NATIVE_ABI_VERSION, &a, 1, &arg, &result) == GOAT_NATIVE_OK);
    assert(result.value.integer == 55 && allocations > 10 && state.first);
    goat_native_deallocate_all(&a);
    assert(!state.first && get_allocated_memory_size() == baseline);
    allocations = 0;
    arg.value.integer = 10000;
    result.value.integer = -123;
    assert(fib->invoke(GOAT_NATIVE_ABI_VERSION, &a, 1, &arg, &result)
           == GOAT_NATIVE_RESOURCE_LIMIT);
    assert(result.value.integer == -123 && allocations > 1 && state.first);
    goat_native_deallocate_all(&a);
    assert(!state.first && get_allocated_memory_size() == baseline);
    return 0;
}
