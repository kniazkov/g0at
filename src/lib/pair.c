/**
 * @file pair.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of functions for working with pairs.
 */

#include "pair.h"

#include <stddef.h>

void *binary_search(pair_t *pairs,
                    size_t size,
                    const void *key,
                    int (*comparator)(const void *, const void *)) {
    size_t low = 0;
    size_t high = size;

    /* Search the half-open interval [low, high), including the empty case. */
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp_result = comparator(pairs[mid].key, key);

        if (cmp_result == 0) {
            return pairs[mid].value;
        }
        if (cmp_result < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return NULL;
}
