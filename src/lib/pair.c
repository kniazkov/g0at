/**
 * @file pair.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of functions for working with pairs.
 *
 * This file contains the implementation of functions for creating, destroying, 
 * and searching pairs (key-value pairs). The `binary_search` function is provided 
 * to search for a key in an array of pairs, using a comparator function.
 */

#include <stddef.h>

#include "pair.h"

void *binary_search(pair_t *pairs, size_t size, const void *key,
        int (*comparator)(const void*, const void*)) {
    size_t low = 0;
    size_t high = size - 1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        int cmp_result = comparator(pairs[mid].key, key);

        if (cmp_result == 0) {
            return pairs[mid].value;
        }
        if (cmp_result < 0) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return NULL;
}
