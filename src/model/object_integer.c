/**
 * @file object_integer.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of an object representing an integer.
 */

#include <stdio.h>
#include <inttypes.h>

#include "object.h"
#include "process.h"
#include "lib/allocate.h"

/**
 * @struct object_integer_t
 * @brief Structure representing an integer object.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int64_t value; ///< The integer value of the object.
} object_integer_t;

/**
 * @brief Releases an integer object.
 * @param obj The object to release.
 */
static void object_integer_release(object_t *obj) {
    object_list_remove(&obj->process->objects, obj);
    FREE(obj);
}

/**
 * @brief Converts an integer object to a string representation.
 * @param obj The object to convert to a string.
 * @return A wide-character string (wchar_t) that represents the object as a string.
 */
static wchar_t *object_integer_to_string(object_t *obj) {
    object_integer_t *int_obj = (object_integer_t *)obj;
    size_t buf_size = 24; // max 20 digits + sign + null terminator
    wchar_t *wstr = (wchar_t *)ALLOC(buf_size * sizeof(wchar_t));
    swprintf(wstr, buf_size, L"%" PRId64, int_obj->value);
    return wstr;
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the integer object.
 */
static object_vtbl_t vtbl = {
    .release = object_integer_release,
    .to_string = object_integer_to_string
};

object_t *object_integer_create(process_t *process, int64_t value) {
    object_integer_t *obj = (object_integer_t *)CALLOC(sizeof(object_integer_t));
    obj->base.vtbl = &vtbl;
    obj->base.process = process;
    obj->value = value;
    object_list_add(&process->objects, &obj->base);
    return &obj->base;
}
