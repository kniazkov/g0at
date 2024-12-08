/**
 * @file object_stack.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements the stack structure and operations for managing Goat objects.
 *
 * This file provides the implementation for a stack that stores Goat objects. It includes
 * functions for creating, pushing, popping, peeking, and destroying the stack.
 */

#include "object_stack.h"
#include "lib/allocate.h"

/**
 * @brief Default initial capacity for the stack.
 */
#define DEFAULT_CAPACITY 128

object_stack_t *object_stack_create() {
    object_stack_t *stack = (object_stack_t *)CALLOC(sizeof(object_stack_t));
    stack->data = (object_t **)ALLOC(DEFAULT_CAPACITY * sizeof(object_t *));
    stack->capacity = DEFAULT_CAPACITY;
    stack->size = 0;
    return stack;
}

void object_stack_push(object_stack_t *stack, object_t *object) {
    if (stack->size == stack->capacity) {
        size_t new_capacity = stack->capacity * 2;
        object_t **new_data = (object_t **)ALLOC(new_capacity * sizeof(object_t *));
        for (size_t i = 0; i < stack->size; i++) {
            new_data[i] = stack->data[i];
        }
        FREE(stack->data);
        stack->data = new_data;
        stack->capacity = new_capacity;
    }
    stack->data[stack->size++] = object;
}

object_t *object_stack_pop(object_stack_t *stack) {
    if (stack->size == 0) {
        return NULL;
    }
    return stack->data[--stack->size];
}

object_t *object_stack_peek(object_stack_t *stack, int index) {
    if (index < 0 || (size_t)index >= stack->size) {
        return NULL;
    }
    return stack->data[stack->size - 1 - index];
}

void object_stack_destroy(object_stack_t *stack) {
    if (stack->data != NULL) {
        FREE(stack->data);
    }
    FREE(stack);
}
