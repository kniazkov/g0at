/**
 * @file object_stack.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the stack structure for managing Goat objects.
 */

#pragma once

#include "common/types.h"

typedef struct object_t object_t;

/** @brief Stack structure for storing Goat objects. */
typedef struct {
    object_t **objects; /**< Array of Goat objects. */
    size_t size;        /**< Number of elements currently in the stack. */
    size_t capacity;    /**< Maximum number of elements the stack can hold. */
} object_stack_t;

/** @brief Creates a new object stack. */
object_stack_t *create_object_stack();

/** @brief Pushes an object onto the stack. */
stack_index_t push_object_onto_stack(object_stack_t *stack, object_t *object);

/**
 * @brief Pops the top object from the stack.
 *
 * If the stack is empty, returns NULL.
 * @return Pointer to the popped object, or NULL if the stack is empty.
 */
object_t *pop_object_from_stack(object_stack_t *stack);

/**
 * @brief Retrieves an object at a specific index from the stack without removing it.
 *
 * If the index is invalid, returns NULL.
 * @return Pointer to the object at the specified index, or NULL if the index is invalid.
 */
object_t *peek_object_from_stack(object_stack_t *stack, stack_index_t index);

/**
 * @brief Replaces an object at a specific index on the stack.
 *
 * Decrements the reference count of the old object at the given index and replaces it with a new
 * object, incrementing its reference count.
 */
void replace_object_on_stack(object_stack_t *stack, object_t *new_object, stack_index_t index);

/**
 * @brief Reduces the size of the object stack to a specified index.
 *
 * Decrements the reference count (`DECREF`) of all objects beyond the new index and updates the
 * stack size accordingly.
 * `new_index`: Index of the element by which the stack is reduced. Must be less than or equal
 * to the current index.
 */
void reduce_object_stack(object_stack_t *stack, stack_index_t new_index);

/** @brief Destroys the stack and frees its resources. */
void destroy_object_stack(object_stack_t *stack);
