/** @file allocation_fault.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic sticky allocation failure for isolated subprocess tests.
 */
#include "allocation_fault.h"
#undef malloc
#undef calloc
#undef realloc
#include <errno.h>
#include <stdlib.h>

static _Thread_local size_t attempts, failure;
static _Thread_local bool active, triggered;
static _Thread_local bool sticky = true;

void allocation_fault_set_sticky(bool enabled) {
    sticky = enabled;
}

void allocation_fault_start(size_t fail_index) {
    attempts = 0;
    failure = fail_index;
    triggered = false;
    active = true;
}

size_t allocation_fault_stop(void) {
    active = false;
    return attempts;
}

bool allocation_fault_triggered(void) {
    return triggered;
}

static bool fail(void) {
    if (!active)
        return false;
    ++attempts;
    bool rejected = failure && (sticky ? attempts >= failure : attempts == failure);
    if (rejected) {
        triggered = true;
        errno = ENOMEM;
    }
    return rejected;
}

void *allocation_fault_malloc(size_t size) {
    return fail() ? NULL : malloc(size);
}

void *allocation_fault_calloc(size_t count, size_t size) {
    return fail() ? NULL : calloc(count, size);
}

void *allocation_fault_realloc(void *pointer, size_t size) {
    return fail() ? NULL : realloc(pointer, size);
}
