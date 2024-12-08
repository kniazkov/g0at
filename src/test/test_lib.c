/**
 * @file test_lib.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library
 */

#include <stdint.h>

#include "test_lib.h"
#include "lib/allocate.h"

bool test_memory_allocation() {
    bool result = true;
    do {
        int i;
        int32_t *array = (int32_t *)CALLOC(sizeof(int32_t) * 16);
        for (i = 0; i < 16; i++) {
            if (array[i] != 0) {
                result = false;
                break;
            }
        }
        if (!result)  {
            break;
        }
        if (get_allocated_memory_size() != sizeof(int32_t) * 16) {
            result = false;
            break;
        }
        FREE(array);
        if (get_allocated_memory_size() != 0) {
            result = false;
            break;
        }
        result = true;
    } while (false);
    return result;
}
