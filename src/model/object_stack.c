/**
 * @file object_stack.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements the stack structure and operations for managing Goat objects.
 *
 * This file provides the implementation for a stack that stores Goat objects. It includes
 * functions for creating, pushing, popping, peeking, and destroying the stack.
 */

#include <assert.h>

#include "object_stack.h"
#include "object.h"
#include "lib/allocate.h"

/**
 * @brief Default initial capacity for the stack.
 */
#define DEFAULT_CAPACITY 128

object_stack_t *create_object_stack() {
    object_stack_t *stack = (object_stack_t *)CALLOC(sizeof(object_stack_t));
    stack->objects = (object_t **)ALLOC(DEFAULT_CAPACITY * sizeof(object_t *));
    stack->capacity = DEFAULT_CAPACITY;
    stack->size = 0;
    return stack;
}

stack_index_t push_object_onto_stack(object_stack_t *stack, object_t *object) {
    if (stack->size == stack->capacity) {
        size_t new_capacity = stack->capacity * 2;
        object_t **new_data = (object_t **)ALLOC(new_capacity * sizeof(object_t *));
        for (size_t index = 0; index < stack->size; index++) {
            new_data[index] = stack->objects[index];
        }
        FREE(stack->objects);
        stack->objects = new_data;
        stack->capacity = new_capacity;
    }
    stack_index_t new_index = stack->size++;
    stack->objects[new_index] = object;
    return new_index;
}

object_t *pop_object_from_stack(object_stack_t *stack) {
    if (stack->size == 0) {
        return NULL;
    }
    return stack->objects[--stack->size];
}

object_t *peek_object_from_stack(object_stack_t *stack, stack_index_t index) {
    if (index < 0 || index >= stack->size) {
        return NULL;
    }
    return stack->objects[stack->size - 1 - index];
}

void reduce_object_stack(object_stack_t *stack, stack_index_t new_index) {
    assert(stack->size > new_index);
    for (size_t index = new_index + 1; index < stack->size; index++) {
        DECREF(stack->objects[index]);
    }
    stack->size = new_index + 1;
}

void replace_object_on_stack(object_stack_t *stack, object_t *new_object, stack_index_t index) {
    assert(index < stack->size);
    object_t *old_object = stack->objects[index];
    if (old_object != new_object) {
        DECREF(old_object);
        stack->objects[index] = new_object;
        INCREF(new_object);
    }
}

void destroy_object_stack(object_stack_t *stack) {
    if (stack->objects != NULL) {
        FREE(stack->objects);
    }
    FREE(stack);
}
