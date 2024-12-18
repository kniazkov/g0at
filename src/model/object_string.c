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
#include "lib/string_ext.h"

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
 * @brief Compares two string objects based on their values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    string_value_t first = obj1->vtbl->to_string(obj1);
    string_value_t second = obj2->vtbl->to_string(obj2);
    int result = wcscmp(first.data, second.data);
    if (second.should_free) {
        FREE(second.data);
    }
    return result;
}

/**
 * @brief Clones a string object.
 * @param process The process that will own the cloned object.
 * @param obj The string object to be cloned.
 * @return A pointer to the cloned string object. If the process is the same, the original object
 *  is returned; otherwise, a new object is created.
 */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    string_value_t value = obj->vtbl->to_string(obj);
    return create_dynamic_string_object(process, value);
}

/**
 * @brief Returns the string data of the static string object.
 * @param obj The static string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t static_to_string(const object_t *obj) {
    object_static_string_t *stsobj = (object_static_string_t *)obj;
    return (string_value_t){ stsobj->data, stsobj->length, false };
}

/**
 * @brief Returns the string data of the dynamic string object.
 * @param obj The dynamic string object to convert to a string.
 * @return A `string_value_t` structure containing the string data and a `false` flag
 *  (indicating the string is not dynamically allocated).
 */
static string_value_t dynamic_to_string(const object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    return (string_value_t){ dsobj->data, dsobj->length, false };
}

/**
 * @brief Converts a string object to a Goat notation string representation.
 * 
 * This function converts the object into a string formatted in Goat notation,
 * escaping special characters like newline, carriage return, tab, quote, and backslash.
 * The resulting string is dynamically allocated and the caller must free it using `FREE`.
 * 
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 *         The string is dynamically allocated, and the caller must free it using `FREE`.
 */
static string_value_t to_string_notation(const object_t *obj) {
    string_value_t value = obj->vtbl->to_string(obj);
    string_builder_t builder;
    init_string_builder(&builder, value.length + 2);
    append_char(&builder, '"');
    for (size_t i = 0; i < value.length; i++) {
        wchar_t ch = value.data[i];
        switch (ch) {
            case '\r':
                append_substring(&builder, L"\\r", 2);
                break;
            case '\n':
                append_substring(&builder, L"\\n", 2);
                break;
            case '\t':
                append_substring(&builder, L"\\t", 2);
                break;
            case '"':
                append_substring(&builder, L"\\\"", 2);
                break;
            case '\\':
                append_substring(&builder, L"\\\\", 2);
                break;
            default:
                append_char(&builder, ch);
        }
    }
    return append_char(&builder, '"');
}

/**
 * @brief Retrieves the value of a property from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve the value of a property from
 * an object. Currently, it returns `NULL` as a placeholder.
 * 
 * @param obj The object from which to retrieve the property.
 * @param key The key of the property to retrieve.
 * @return Always returns `NULL` (placeholder implementation).
 */
static object_t *get_property(object_t *obj, object_t *key) {
    return NULL;
}

/**
 * @brief Attempts to set a property on an object.
 * 
 * This function is used to set a property on an object. However, since the object is
 * immutable, no property can be added or modified. Therefore, the function always
 * returns `false`.
 * 
 * @param obj The immutable object on which to set the property.
 * @param key The key of the property to set.
 * @param value The value to assign to the property.
 * @return Always returns `false` because the object is immutable.
 */
static bool set_property(object_t *obj, object_t *key, object_t *value) {
    return false;
}

/**
 * @brief Adds two objects together, concatenating their string representations.
 * 
 * This function converts the two objects to their string representations and concatenates them
 * into a new string object. If either of the objects is an empty string, the other object is
 * returned as the result. If both objects have non-empty string representations, their strings are
 * concatenated into a new dynamic string object.
 * 
 * @param process Process that will own the resulting object.
 * @param obj1 The first object to add (string, static or dynamic).
 * @param obj2 The second object to add (converted to string).
 * @return A pointer to the resulting object after concatenation.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    string_value_t first = obj1->vtbl->to_string(obj1);
    if (first.length == 0) {
        if (obj2->vtbl->type == TYPE_STRING) {
            INCREF(obj2);
            return obj2;
        }
        return create_dynamic_string_object(process, obj2->vtbl->to_string(obj2));
    }
    string_value_t second = obj2->vtbl->to_string(obj2);
    if (second.length == 0) {
        INCREF(obj1);
        return obj1;
    }
    string_builder_t builder;
    init_string_builder(&builder, first.length + second.length);
    append_substring(&builder, first.data, first.length);
    string_value_t result = append_substring(&builder, second.data, second.length);
    if (second.should_free) {
        FREE(second.data);
    }
    return create_dynamic_string_object(process, result);
}

/**
 * @brief Subtracts one object from another.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return Always returns `false`, indicating that subtraction is not supported.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Retrieves the boolean representation of a string object.
 * 
 * This function converts the string object to its string representation and returns `true` if the 
 * string is non-empty, and `false` otherwise. An empty string is considered as a falsy value.
 * 
 * @param obj The object from which to retrieve the boolean value.
 * @return `true` if the string is non-empty, `false` if the string is empty.
 */

static bool get_boolean_value(const object_t *obj) {
    string_value_t value = obj->vtbl->to_string(obj);
    return value.length > 0;
}

/**
 * @brief Retrieves the integer value of a string object.
 * @param obj The string object.
 * @return An invalid `int_value_t` indicating that string objects cannot be converted
 *  to integers.
 */
static int_value_t get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves the real value of a string object.
 * @param obj The string object.
 * @return An invalid `real_value_t` indicating that string objects cannot be converted
 *  to real numbers.
 */
static real_value_t get_real_value(const object_t *obj) {
    return (real_value_t){ false, 0.0 };
}

/**
 * @var static_string_vtbl
 * @brief Virtual table defining the behavior of the static string object.
 */
static object_vtbl_t static_string_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = memory_function_stub,
    .dec_ref = memory_function_stub,
    .mark = memory_function_stub,
    .sweep = memory_function_stub,
    .release = memory_function_stub,
    .compare = compare,
    .clone = clone,
    .to_string = static_to_string,
    .to_string_notation = to_string_notation,
    .get_property = get_property,
    .set_property = set_property,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
};

object_t *create_static_string_object(wchar_t *data, size_t length) {
    object_static_string_t *obj = (object_static_string_t *)CALLOC(sizeof(object_static_string_t));
    obj->base.vtbl = &static_string_vtbl;
    obj->data = data;
    obj->length = length;
    return &obj->base;
}

/**
 * @brief Macro to declare a static string object and provide access to it.
 * 
 * This macro defines a static string object with a specified name and string value. It also
 * provides a function to retrieve the object. The string is stored as a constant literal, and
 * the object is initialized with the appropriate virtual table.
 * 
 * @param name The name of the static string object.
 * @param string The wide-character string literal.
 */
#define DECLARE_STATIC_STRING(name, string) \
    static object_static_string_t name = \
        { { &static_string_vtbl, NULL, NULL, NULL }, (string), sizeof(string) - 1 }; \
    object_t *get_##name() { return &name.base; }

/**
 * @brief Declares some common static string objects.
 */
DECLARE_STATIC_STRING(empty_string, L"")
DECLARE_STATIC_STRING(string_print, L"print")

/**
 * @var dynamic_string_vtbl
 * @brief Virtual table defining the behavior of the dynamic string object.
 */
static object_vtbl_t dynamic_string_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = inc_ref,
    .dec_ref = dec_ref,
    .mark = mark,
    .sweep = sweep,
    .release = release,
    .compare = compare,
    .clone = clone,
    .to_string = dynamic_to_string,
    .to_string_notation = to_string_notation,
    .get_property = get_property,
    .set_property = set_property,
    .add = add,
    .sub = sub,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
};

object_t *create_dynamic_string_object(process_t *process, string_value_t value) {
    if (value.length == 0) {
        if (value.should_free) {
            FREE(value.data);
        }
        return get_empty_string();
    }
    object_dynamic_string_t *obj;
    if (process->dynamic_strings.size > 0) {
        obj = (object_dynamic_string_t *)remove_first_object_from_list(&process->dynamic_strings);
    } else {
        obj = (object_dynamic_string_t *)CALLOC(sizeof(object_dynamic_string_t));
        obj->base.vtbl = &dynamic_string_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->data = value.should_free ? value.data : WSTRDUP(value.data);
    obj->length = value.length;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
