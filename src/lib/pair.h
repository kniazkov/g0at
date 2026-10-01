/**
 * @file pair.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a generic pair.
 */

#pragma once

#include <stddef.h>

/** @brief A structure representing a key-value pair. */
typedef struct {
    void *key;   /**< A pointer to the key of the pair. */
    void *value; /**< A pointer to the value of the pair. */
} pair_t;

/**
 * @brief Performs a binary search for a key in an array of pairs.
 *
 * The array must be sorted by the key values, and a comparator function is used to compare keys.
 * @return A pointer to the corresponding value if the key is found, or NULL if the key is not
 * found.
 */
void *binary_search(pair_t *pairs, size_t size, const void *key,
    int (*comparator)(const void*, const void*));
