/** @file allocation_fault.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-build-only allocation failure injection, including third-party code.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
/* Do not rewrite imported CRT declarations into imported test functions. */
#    include <malloc.h>
#    include <stdlib.h>
#endif

void allocation_fault_set_sticky(bool enabled);
void allocation_fault_start(size_t fail_index);
size_t allocation_fault_stop(void);
bool allocation_fault_triggered(void);
void *allocation_fault_malloc(size_t size);
void *allocation_fault_calloc(size_t count, size_t size);
void *allocation_fault_realloc(void *pointer, size_t size);

#ifdef GOAT_TEST_ALLOC_FAILURE
#    define malloc allocation_fault_malloc
#    define calloc allocation_fault_calloc
#    define realloc allocation_fault_realloc
#endif
