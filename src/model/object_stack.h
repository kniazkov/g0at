/**
 * @file object_stack.h 
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the stack structure for managing Goat objects.
 *
 * This file describes the structure and operations for a stack of Goat objects.
 *
 * The object stack stores objects based on the Last-In-First-Out (LIFO) principle.
 * This means that the most recently added object is the first one to be removed.
 * 
 * The object stack is a fundamental data structure in a stack-based virtual machine. 
 * It holds all the execution thread data, including intermediate results, local variables, 
 * and return addresses. All operations in such a virtual machine are performed on the stack, 
 * making it the central mechanism for function calls, parameter passing, and expression evaluation.
 *
 * Each instruction in the virtual machine typically operates by pushing operands onto the stack 
 * and popping results from it, creating a simple and efficient execution model. This design 
 * minimizes the need for complex data structures and allows for a compact and easy-to-implement 
 * instruction set.
 */

#pragma once

#include "common/types.h"

/**
 * @typedef object_t
 * @brief Forward declaration for the object structure.
 */
typedef struct object_t object_t;

/**
 * @struct object_stack_t
 * @brief Stack structure for storing Goat objects.
 */
typedef struct {
    object_t **objects;  /**< Array of Goat objects. */
    size_t size;         /**< Number of elements currently in the stack. */
    size_t capacity;     /**< Maximum number of elements the stack can hold. */
} object_stack_t;

/**
 * @brief Creates a new object stack.
 * @return Pointer to the newly created stack.
 */
object_stack_t *create_object_stack();

/**
 * @brief Pushes an object onto the stack.
 *
 * Adds a new object to the top of the stack. If the stack is full, its capacity is
 * automatically doubled to accommodate more objects.
 * 
 * @param stack A pointer to the stack.
 * @param object A pointer to the object to push onto the stack.
 * @return The stack index of the newly pushed object.
 */
stack_index_t push_object_onto_stack(object_stack_t *stack, object_t *object);

/**
 * @brief Pops the top object from the stack.
 *
 * Removes and returns the object at the top of the stack. If the stack is empty,
 * returns NULL.
 * 
 * @param stack A pointer to the stack.
 * @return Pointer to the popped object, or NULL if the stack is empty.
 */
object_t *pop_object_from_stack(object_stack_t *stack);

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
object_t *peek_object_from_stack(object_stack_t *stack, stack_index_t index);

/**
 * @brief Replaces an object at a specific index on the stack.
 *
 * Decrements the reference count of the old object at the given index and replaces it
 * with a new object, incrementing its reference count. If the new object is the same
 * as the old one, no action is taken.
 *
 * @param stack A pointer to the stack.
 * @param new_object A pointer to the new object to place on the stack.
 * @param index The index at which to replace the object.
 */
void replace_object_on_stack(object_stack_t *stack, object_t *new_object, stack_index_t index);

/**
 * @brief Reduces the size of the object stack to a specified index.
 *
 * Decrements the reference count (`DECREF`) of all objects beyond the new index
 * and updates the stack size accordingly. Used to clean up objects when 
 * exiting a scope or unwinding the stack.
 *
 * @param stack A pointer to the stack.
 * @param new_index Index of the element by which the stack is reduced. 
 *  Must be less than or equal to the current index.
 */
void reduce_object_stack(object_stack_t *stack, stack_index_t new_index);

/**
 * @brief Destroys the stack and frees its resources.
 * @param stack A pointer to the stack.
 */
void destroy_object_stack(object_stack_t *stack);
