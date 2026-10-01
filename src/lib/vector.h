/**
 * @file vector.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a vector (dynamic array).
 */

#pragma once

#include <stddef.h>

/** @brief A structure representing a dynamic array (vector) of pointers. */
typedef struct {
    void **data;     /**< Pointer to the array of pointers (the actual elements). */
    size_t size;     /**< The current number of elements in the vector. */
    size_t capacity; /**< The current capacity of the vector. */
} vector_t;

/** @brief Creates a new vector with an initial capacity of 0. */
vector_t *create_vector();

/** @brief Creates a new vector with a specified initial capacity. */
vector_t *create_vector_ex(size_t init_capacity);

/** @brief Adds an item to the vector. */
void append_to_vector(vector_t *vector, void *item);

/**
 * @brief Reverses the order of elements in a vector.
 *
 * After the operation, the first element becomes the last, the second becomes the second-to-last,
 * and so on. The function operates in-place and does not require additional memory allocation.
 */
void reverse_vector(vector_t *vector);

/** @brief Clears the contents of a vector without deallocating it. */
void clear_vector(vector_t *vector);

/**
 * @brief Destroys the vector and frees all associated memory.
 *
 * Frees the memory used by the vector itself, but does not free the memory of the items stored in
 * the vector.
 */
void destroy_vector(vector_t *vector);

/** @brief Destroys the vector and frees all associated memory, including the items. */
void destroy_vector_ex(vector_t *vector, void (*item_dtor)(void *));
