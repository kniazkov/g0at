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

/** @brief Reports a fatal stack invariant violation and exits the process. */
_Noreturn void fail_stack_underflow(void);

/** @brief Requires enough stack values, otherwise prints to stderr and exits. */
void require_object_stack_size(const object_stack_t *stack, size_t size);

/** @brief Pops a value; stack underflow is fatal. */
object_t *pop_object_from_stack(object_stack_t *stack);

/** @brief Peeks from the top; an out-of-range index is fatal. */
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
