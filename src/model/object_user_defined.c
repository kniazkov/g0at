/**
 * @file object_user_defined.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions and methods for user-defined objects in the Goat programming language.
 * 
 * This file provides the structure and functionality for a user-defined object type in the
 * Goat programming language. A user-defined object is a general-purpose object that allows storing
 * arbitrary data and methods in the form of key-value pairs.
 */

#include <assert.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"

/**
 * @def POOL_CAPACITY
 * @brief Defines the maximum capacity of the object pool.
 * 
 * This macro sets the maximum number of objects that can be stored in the object pool 
 * before it reaches its capacity. Once the pool is full, any swept objects are destroyed 
 * instead of being added to the pool.
 */
#define POOL_CAPACITY 1024

/**
 * @struct object_user_defined_t
 * @brief Structure representing an user-defined object.
 */
typedef struct {
    /**
     * @brief The base object that provides common functionality.
     */
    object_t base;
    
    /**
     * @brief Reference count.
     */
    int refs;

    /**
     * @brief The state of the object (e.g., unmarked, marked, or zombie).
     */
    object_state_t state;

    /**
     * @brief AVL tree storing key-value pairs, where both keys and values are objects.
     */
    avl_tree_t *children;
} object_user_defined_t;
