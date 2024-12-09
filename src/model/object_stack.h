/**
 * @file object_stack.h 
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the stack structure for managing Goat objects.
 *
 * This file describes the structure and operations for a stack of Goat objects.
 */

#pragma once

#include <stddef.h>

/**
 * @typedef object_t
 * @brief Forward declaration for the object structure.
 */
typedef struct object_t object_t;

/**
 * @struct object_stack
 * @brief Stack structure for storing Goat objects.
 */
typedef struct {
    object_t **data;     /**< Array of Goat objects. */
    size_t size;         /**< Number of elements currently in the stack. */
    size_t capacity;     /**< Maximum number of elements the stack can hold. */
} object_stack_t;

/**
 * @brief Creates a new object stack.
 * @return Pointer to the newly created stack.
 */
object_stack_t *object_stack_create();

/**
 * @brief Pushes an object onto the stack.
 *
 * Adds a new object to the top of the stack. If the stack is full, its capacity is
 * automatically doubled to accommodate more objects.
 * 
 * @param stack A pointer to the stack.
 * @param object A pointer to the object to push onto the stack.
 */
void object_stack_push(object_stack_t *stack, object_t *object);

/**
 * @brief Pops the top object from the stack.
 *
 * Removes and returns the object at the top of the stack. If the stack is empty,
 * returns NULL.
 * 
 * @param stack A pointer to the stack.
 * @return Pointer to the popped object, or NULL if the stack is empty.
 */
object_t *object_stack_pop(object_stack_t *stack);

/**
 * @brief Retrieves an object at a specific index from the stack without removing it.
 *
 * Allows access to any object in the stack by its index, where 0 represents the top of
 * the stack. If the index is invalid, returns NULL.
 * 
 * @param stack A pointer to the stack.
 * @param index The index of the object (0 is the top of the stack).
 * @return Pointer to the object at the specified index, or NULL if the index is invalid.
 */
object_t *object_stack_peek(object_stack_t *stack, int index);

/**
 * @brief Destroys the stack and frees its resources.
 * @param stack A pointer to the stack.
 */
void object_stack_destroy(object_stack_t *stack);
