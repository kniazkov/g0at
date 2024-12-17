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
 * @brief Compares integer object and other numeric object based on their values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    object_integer_t *iobj1 = (object_integer_t *)obj1;
    double diff = iobj1->value - obj2->vtbl->get_real_value(obj2).value;
    if (diff > 0) {
        return 1;
    } else if (diff < 0) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief Clones an integer object.
 * @param process The process that will own the cloned object.
 * @param obj The integer object to be cloned.
 * @return A pointer to the cloned integer object. If the process is the same, the original object
 *  is returned; otherwise, a new object is created.
 */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    object_integer_t *iobj = (object_integer_t *)obj;
    return create_integer_object(process, iobj->value);
}

/**
 * @brief Converts an integer object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` structure containing the string representation of the object.
 *  The string is dynamically allocated and the caller must free it using `FREE`.
 */
static string_value_t to_string(const object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    size_t buf_size = 24; // max 20 digits + sign + null terminator
    wchar_t *wstr = (wchar_t *)ALLOC(buf_size * sizeof(wchar_t));
    swprintf(wstr, buf_size, L"%" PRId64, iobj->value);
    return (string_value_t){ wstr, wcslen(wstr), true };
}

/**
 * @brief Converts an integer object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 *  The string is dynamically allocated and the caller must free it using `FREE`.
 */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/**
 * @brief Adds two objects and returns the result as a new object.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object to add.
 * @param obj2 The second object to add.
 * @return A pointer to the resulting object of the addition, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t second = obj2->vtbl->get_integer_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    object_integer_t *first = (object_integer_t *)obj1;
    return create_integer_object(process, first->value + second.value);
}

/**
 * @brief Subtracts the value of the second object from the first object.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return A pointer to the resulting object of the subtraction, or `NULL` if the second object 
 *  cannot be interpreted as an integer.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t second = obj2->vtbl->get_integer_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    object_integer_t *first = (object_integer_t *)obj1;
    return create_integer_object(process, first->value - second.value);
}

/**
 * @brief Retrieves the boolean representation of an object.
 * @param obj The object from which to retrieve the boolean value.
 * @return Boolean representation of the object.
 */
static bool get_boolean_value(const object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    return iobj->value != 0;
}

/**
 * @brief Retrieves the integer value of an object.
 * @param obj The object from which to retrieve the integer value.
 * @return An `int_value_t` structure containing the integer value.
 */
static int_value_t get_integer_value(const object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    return (int_value_t){ true, iobj->value };
}

/**
 * @brief Retrieves value of an object casted to real.
 * @param obj The object from which to retrieve the real value.
 * @return A `real_value_t` structure containing the real value.
 */
static real_value_t get_real_value(const object_t *obj) {
    object_integer_t *iobj = (object_integer_t *)obj;
    return (real_value_t){ true, (double)iobj->value };
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the integer object.
 */
static object_vtbl_t vtbl = {
    .type = TYPE_NUMBER,
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .compare = compare,
    .clone = clone,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
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
