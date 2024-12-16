/**
 * @file object_string.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing a string.
 *
 * This file defines the structure and behavior of string objects. There are two types
 * of string objects:
 * 1. Static strings:
 *    - These include strings declared in the model, such as identifiers of built-in functions
 *      (e.g., `print`).
 *    - They can also be strings loaded from bytecode.
 *    - Static strings only store a reference to their data, which exists for the duration of the
 *      program's execution. These strings are not managed by the garbage collector.
 * 2. Dynamic strings:
 *    - These are created as a result of string operations at runtime.
 *    - They internally store their own data array and are subject to garbage collection
 *      when no longer in use.
 */

#include <assert.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "lib/allocate.h"

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
 * @struct object_static_string_t
 * @brief Structure representing a static string object.
 * 
 * Static strings are immutable and exist for the entire duration of the program's execution.
 * They only hold a reference to their data, which is not managed by the garbage collector.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    wchar_t *data; ///< Pointer to the string data (wide character array).
    size_t length; ///< Length of the string (number of characters).
} object_static_string_t;

/**
 * @struct object_dynamic_string_t
 * @brief Structure representing a dynamic string object.
 * 
 * Dynamic strings are also immutable but are created at runtime, typically as a result of
 * string operations. These strings manage their own data and are subject to garbage collection
 * when no longer used.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count used for garbage collection.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    wchar_t *data; ///< Pointer to the string data (wide character array).
    size_t length; ///< Length of the string (number of characters).
} object_dynamic_string_t;

/**
 * @brief A stub memory management function for static strings.
 * @param obj The object. This parameter is unused.
 * @note This function does nothing because static strings do not require memory management.
 */
static void memory_function_stub(object_t *obj) {
    return;
}

/**
 * @brief Releases or clears a dynamic string object.
 * 
 * This function manages the lifecycle of a dynamic string object. If the pool of reusable 
 * objects (`dynamic_strings`) in the process has reached its capacity, the object is fully 
 * deallocated. Otherwise, the object is reset (its data memory is freed, and its internal 
 * fields are cleared) and added to the pool for reuse, reducing the overhead of allocating 
 * new objects.
 * 
 * @param dsobj The dynamic string object to release or clear.
 */
static void release_or_clear(object_dynamic_string_t *dsobj) {
    FREE(dsobj->data);
    remove_object_from_list(&dsobj->base.process->objects, &dsobj->base);
    if (dsobj->base.process->dynamic_strings.size == POOL_CAPACITY) {
        FREE(dsobj);
    } else {
        dsobj->refs = 0;
        dsobj->state = ZOMBIE;
        dsobj->data = NULL;
        dsobj->length = 0;
        add_object_to_list(&dsobj->base.process->dynamic_strings, &dsobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (!(--dsobj->refs)) {
        release_or_clear(dsobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (dsobj->state == UNMARKED) {
        release_or_clear(dsobj);
    } else {
        dsobj->state = UNMARKED;
    }
}

/**
 * @brief Releases a dynamic string object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    remove_object_from_list(
        dsobj->state == ZOMBIE ? &obj->process->dynamic_strings : &obj->process->objects, obj
    );
    FREE(dsobj->data);
    FREE(obj);
}

/**
 * @brief Returns the string data of the static string object.
 * @param obj The static string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t static_to_string(object_t *obj) {
    object_static_string_t *stsobj = (object_static_string_t *)obj;
    return (string_value_t){ stsobj->data, stsobj->length, false };
}

/**
 * @brief Returns the string data of the dynamic string object.
 * @param obj The dynamic string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t dynamic_to_string(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    return (string_value_t){ dsobj->data, dsobj->length, false };
}
