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

vector_t *create_vector() {
    vector_t *vector = (vector_t *)ALLOC(sizeof(vector_t));
    vector->data = NULL;
    vector->size = 0;
    vector->capacity = 0;
    return vector;
}

vector_t *create_vector_ex(size_t init_capacity) {
    vector_t *vector = (vector_t *)ALLOC(sizeof(vector_t));
    vector->data = init_capacity > 0 ? (void **)ALLOC(init_capacity * sizeof(void *)) : NULL;
    vector->size = 0;
    vector->capacity = init_capacity;
    return vector;
}

void append_to_vector(vector_t *vector, void *item) {
    if (vector->size >= vector->capacity) {
        vector->capacity = (vector->capacity == 0) ? 1 : vector->capacity * 2;
        void **new_data = (void **)ALLOC(vector->capacity * sizeof(void *));
        for (size_t index = 0; index < vector->size; index++) {
            new_data[index] = vector->data[index];
        }
        FREE(vector->data);
        vector->data = new_data;
    }
    vector->data[vector->size++] = item;
}

void reverse_vector(vector_t *vector) {
    if (vector->size < 2) {
        return;
    }

    size_t left = 0;
    size_t right = vector->size - 1;

    while (left < right) {
        void *temp = vector->data[left];
        vector->data[left] = vector->data[right];
        vector->data[right] = temp;
        left++;
        right--;
    }
}

void clear_vector(vector_t *vector) {
    vector->size = 0;
}

void destroy_vector(vector_t *vector) {
    FREE(vector->data);
    FREE(vector);
}

void destroy_vector_ex(vector_t *vector, void (*item_dtor)()) {
    for (size_t index = 0; index < vector->size; index++) {
        item_dtor(vector->data[index]);
    }
    FREE(vector->data);
    FREE(vector);
}
