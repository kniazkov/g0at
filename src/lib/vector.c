/**
 * @file vector.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of functions for a dynamic array (vector).
 *
 * This source file contains the implementation of the functions for the vector data structure.
 * The vector is a dynamically resizing array that can store pointers to arbitrary data types.
 * It provides functionality for adding elements, resizing the array when necessary, and destroying 
 * the vector along with its elements.
 */

#include "vector.h"
#include "allocate.h"

vector_t *vector_create() {
    vector_t *vector = (vector_t *)ALLOC(sizeof(vector_t));
    vector->data = NULL;
    vector->size = 0;
    vector->capacity = 0;
    return vector;
}

vector_t *vector_create_ex(size_t init_capacity) {
    vector_t *vector = (vector_t *)ALLOC(sizeof(vector_t));
    vector->data = (void **)ALLOC(init_capacity * sizeof(void *));
    vector->size = 0;
    vector->capacity = init_capacity;
    return vector;
}

void vector_add(vector_t *vector, void *item) {
    if (vector->size >= vector->capacity) {
        vector->capacity = (vector->capacity == 0) ? 1 : vector->capacity * 2;
        void **new_data = (void **)ALLOC(vector->capacity * sizeof(void *));
        for (size_t i = 0; i < vector->size; i++) {
            new_data[i] = vector->data[i];
        }
        FREE(vector->data);
        vector->data = new_data;
    }
    vector->data[vector->size++] = item;
}

void vector_destroy(vector_t *vector) {
    FREE(vector->data);
    FREE(vector);
}

void vector_destroy_ex(vector_t *vector, void (*item_dtor)()) {
    for (size_t i = 0; i < vector->size; i++) {
        item_dtor(vector->data[i]);
    }
    FREE(vector->data);
    FREE(vector);
}
