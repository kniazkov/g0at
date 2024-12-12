/**
 * @file vector.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a vector (dynamic array).
 *
 * This header file provides the interface for a simple dynamic array, referred to as a vector.
 * A vector allows for storing elements of any type and dynamically resizing itself as elements 
 * are added. It uses a resizable array structure and automatically handles memory allocation 
 * and deallocation for its elements.
 */

#pragma once

#include <stddef.h>

/**
 * @struct vector_t
 * @brief A structure representing a dynamic array (vector) of pointers.
 */
typedef struct {
    void **data;     /**< Pointer to the array of pointers (the actual elements). */
    size_t size;     /**< The current number of elements in the vector. */
    size_t capacity; /**< The current capacity of the vector. */
} vector_t;

/**
 * @brief Creates a new vector with an initial capacity of 0.
 * 
 * This function initializes a vector with no elements and a zero capacity.
 * The vector will need to be resized as elements are added.
 * 
 * @return A pointer to the created vector.
 */
vector_t *create_vector();

/**
 * @brief Creates a new vector with a specified initial capacity.
 * 
 * This function initializes a vector with no elements but with a given 
 * initial capacity. The vector will grow as needed when elements are added.
 * 
 * @param init_capacity Initial capacity of the vector.
 * @return A pointer to the created vector.
 */
vector_t *create_vector_ex(size_t init_capacity);

/**
 * @brief Adds an item to the vector.
 * 
 * If the vector's current capacity is not enough, it will be resized to accommodate
 * more items. The new item will be added at the end of the vector.
 * 
 * @param vector A pointer to the vector to which the item will be added.
 * @param item The item to be added to the vector.
 */
void append_to_vector(vector_t *vector, void *item);

/**
 * @brief Destroys the vector and frees all associated memory.
 * 
 * This function frees the memory used by the vector itself, but does not 
 * free the memory of the items stored in the vector.
 * 
 * @param vector A pointer to the vector to be destroyed.
 */
void destroy_vector(vector_t *vector);

/**
 * @brief Destroys the vector and frees all associated memory, including the items.
 * 
 * This function frees the memory used by the vector itself and also calls 
 * the provided destructor function on each item in the vector.
 * 
 * @param vector A pointer to the vector to be destroyed.
 * @param item_dtor A function pointer that will be called to destroy each item.
 */
void destroy_vector_ex(vector_t *vector, void (*item_dtor)());
