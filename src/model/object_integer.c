/**
 * @file object_integer.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing an integer.
 */

#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

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
 * @struct object_integer_t
 * @brief Structure representing an integer object.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    int64_t value; ///< The integer value of the object.
} object_integer_t;

/**
 * @brief Releases or clears an integer object.
 * 
 * This function either frees the object or resets its state and moves it to a list of reusable
 * objects, depending on the number of objects in the pool.
 * 
 * @param iobj The integer object to release or clear.
 */
static void release_or_clear(object_integer_t *iobj) {
    remove_object_from_list(&iobj->base.process->objects, &iobj->base);
    if (iobj->base.process->integers.size == POOL_CAPACITY) {
        FREE(iobj);
    } else {
        iobj->refs = 0;
        iobj->state = ZOMBIE;
        iobj->value = 0;
        add_object_to_list(&iobj->base.process->integers, &iobj->base);
    }
}

/**
 * @brief Increments the reference count of an object.
 * @param obj The object whose reference count is to be incremented.
 */
static void inc_ref(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    assert(iobj->state != ZOMBIE);
    iobj->refs++;
}

/**
 * @brief Decrements the reference count of an object.
 * @param obj The object whose reference count is to be decremented.
 */
static void dec_ref(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    assert(iobj->state != ZOMBIE);
    if (!(--iobj->refs)) {
        release_or_clear(iobj);
    }
}

/**
 * @brief Marks an object as reachable during garbage collection.
 * @param obj The object to mark as reachable.
 */
static void mark(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    assert(iobj->state != ZOMBIE);
    iobj->state = MARKED;
}

/**
 * @brief Sweeps the object, cleaning it up or moving it to the object pool.
 * @param obj The object to sweep.
 */
static void sweep(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    assert(iobj->state != ZOMBIE);
    if (iobj->state == UNMARKED) {
        release_or_clear(iobj);
    } else {
        iobj->state = UNMARKED;
    }
}

/**
 * @brief Releases an integer object.
 * @param obj The object to release.
 */
static void release(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    remove_object_from_list(
        iobj->state == ZOMBIE ? &obj->process->integers : &obj->process->objects, obj
    );
    FREE(obj);
}

/**
 * @brief Converts an integer object to a string representation.
 * @param obj The object to convert to a string.
 * @return A wide-character string (wchar_t) that represents the object as a string.
 */
static wchar_t *to_string(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    size_t buf_size = 24; // max 20 digits + sign + null terminator
    wchar_t *wstr = (wchar_t *)ALLOC(buf_size * sizeof(wchar_t));
    swprintf(wstr, buf_size, L"%" PRId64, iobj->value);
    return wstr;
}

/**
 * @brief Converts an integer object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A wide-character string (wchar_t) that represents the object as a string.
 */
static wchar_t *to_string_notation(object_t *obj) {
    return to_string(obj);
}

/**
 * @brief Adds two objects and returns the result as a new object.
 * @param obj1 The first object to add.
 * @param obj2 The second object to add.
 * @return A pointer to the resulting object of the addition, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *add(object_t *obj1, object_t *obj2) {
    const int64_t *value2_ptr = obj2->vtbl->get_integer_value(obj2);
    if (!value2_ptr) {
        return NULL;
    }
    object_integer_t *iobj1 = (object_integer_t *)obj1;
    return create_integer_object(obj1->process, iobj1->value + *value2_ptr);
}

/**
 * @brief Subtracts the value of the second object from the first object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return A pointer to the resulting object of the subtraction, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *sub(object_t *obj1, object_t *obj2) {
    const int64_t *value2_ptr = obj2->vtbl->get_integer_value(obj2);
    if (!value2_ptr) {
        return NULL;
    }
    object_integer_t *iobj1 = (object_integer_t *)obj1;
    return create_integer_object(obj1->process, iobj1->value - *value2_ptr);
}

/**
 * @brief Retrieves the pointer to integer value of an object.
 * @param obj The object to retrieve the integer value from.
 * @return A pointer to the integer value stored within the object.
 */
static const int64_t *get_integer_value(object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    return &iobj->value;
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the integer object.
 */
static object_vtbl_t vtbl = {
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .add = add,
    .sub = sub,
    .get_integer_value = get_integer_value
};

object_t *create_integer_object(process_t *process, int64_t value) {
    object_integer_t *obj;
    if (process->integers.size > 0) {
        obj = (object_integer_t *)remove_first_object_from_list(&process->integers);
    } else {
        obj = (object_integer_t *)CALLOC(sizeof(object_integer_t));
        obj->base.vtbl = &vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
