/**
 * @file pair.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a generic pair.
 *
 * This header file provides the interface for a simple pair structure, where each pair consists
 * of a key and a value. Both the key and value can be of any type, and they are stored as pointers.
 */

#pragma once

#include <stddef.h>

/**
 * @struct pair_t
 * @brief A structure representing a key-value pair.
 *
 * This structure stores a key and a value, both of which are pointers to void. 
 * It allows for the creation of key-value pairs where both key and value can be of any type.
 */
typedef struct {
    void *key;   /**< A pointer to the key of the pair. */
    void *value; /**< A pointer to the value of the pair. */
} pair_t;

/**
 * @brief Performs a binary search for a key in an array of pairs.
 *
 * This function performs a binary search on a sorted array of `pair_t` elements, looking for
 * a pair with a key matching the specified one. The array must be sorted by the key values,
 * and a comparator function is used to compare keys.
 *
 * @param pairs A pointer to the array of pairs to search through.
 * @param size The number of elements in the array.
 * @param key A pointer to the key to search for.
 * @param comparator A function pointer for comparing keys.
 * @return A pointer to the corresponding value if the key is found, or NULL if the key
 *  is not found.
 */
void *binary_search(pair_t *pairs, size_t size, void *key, int (*comparator)(void*, void*));
