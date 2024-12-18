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
 * @brief Structure representing a user-defined object.
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

/**
 * @brief Decrements the reference count of a key-value pair in the user-defined object's children.
 * 
 * This function is called for each key-value pair in the AVL tree of a user-defined object. 
 * It decreases the reference count of both the key and the value, ensuring proper memory 
 * management and cleanup of referenced objects.
 * 
 * @param unused Unused parameter, included for compatibility with the AVL tree traversal function.
 * @param key The key of the key-value pair, cast to an object.
 * @param value The value of the key-value pair, stored as a `value_t` type.
 */
static void clear_child_pair(void *unused, void *key, value_t value) {
    DECREF((object_t *)key);
    DECREF((object_t *)value.ptr);
}

/**
 * @brief Clears a user-defined object.
 * 
 * This function iterates through all key-value pairs stored in the children AVL tree of 
 * a user-defined object, decrementing their reference counts and freeing associated memory. 
 * After clearing the contents, it empties the AVL tree but does not deallocate the tree structure.
 * 
 * @param uobj A pointer to the user-defined object to clear.
 */
static void clear(object_user_defined_t *uobj) {
    avl_tree_for_each(uobj->children, clear_child_pair, NULL);
    clear_avl_tree(uobj->children);
}

/**
 * @brief Releases or clears a user-defined object.
 * 
 * This function either frees the object or resets its state and moves it to a list of reusable
 * objects, depending on the number of objects in the pool.
 * 
 * @param iobj The user-defined object to release or clear.
 */
static void release_or_clear(object_user_defined_t *uobj) {
    clear(uobj);
    remove_object_from_list(&uobj->base.process->objects, &uobj->base);
    if (uobj->base.process->user_defined_objects.size == POOL_CAPACITY) {
        destroy_avl_tree(uobj->children);
        FREE(uobj);
    } else {
        uobj->refs = 0;
        uobj->state = ZOMBIE;
        add_object_to_list(&uobj->base.process->user_defined_objects, &uobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    uobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (!(--uobj->refs)) {
        release_or_clear(uobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    uobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (uobj->state == UNMARKED) {
        release_or_clear(uobj);
    } else {
        uobj->state = UNMARKED;
    }
}

/**
 * @brief Releases a user-defined object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    remove_object_from_list(
        uobj->state == ZOMBIE ? &obj->process->user_defined_objects : &obj->process->objects, obj
    );
    clear(uobj);
    destroy_avl_tree(uobj->children);
    FREE(obj);
}
