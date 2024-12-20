/**
 * @file common_methods.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements common methods for the Goat objects.
 * 
 * This file provides implementations of methods that are shared across multiple 
 * object implementations in the Goat language. These methods are defined separately 
 * to ensure code reuse and maintainability.
 */

#include "common_methods.h"

void memory_function_stub(object_t *obj) {
    return;
}

int compare_object_addresses(const object_t *obj1, const object_t *obj2) {
    if (obj1 > obj2) {
        return 1;
    } else if (obj1 < obj2) {
        return -1;
    } else {
        return 0;
    }
}
