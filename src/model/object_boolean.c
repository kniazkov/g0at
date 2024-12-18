/**
 * @file object_boolean.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of two singleton objects representing the boolean values
 *  `true` and `false`.
 */

#include "object.h"
#include "process.h"

/**
 * @brief A stub memory management function for singleton objects.
 * @param obj The object. This parameter is unused.
 * @note This function does nothing because boolean objects are singletons and do not require
 *  memory management.
 */
static void memory_function_stub(object_t *obj) {
    return;
}

/**
 * @brief Compares two boolean objects based on their boolean values.
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the relative order: positive if obj1 > obj2,
 *  negative if obj1 < obj2, 0 if equal.
 */
static int compare(const object_t *obj1, const object_t *obj2) {
    return (obj1->vtbl->get_boolean_value(obj1) ? 1 : 0) 
        - (obj2->vtbl->get_boolean_value(obj2) ? 1 : 0);
}

/**
 * @brief Clones a boolean object.
 * 
 * Since boolean objects are singletons, the clone function simply returns
 * the same object without creating a new instance.
 * 
 * @param process The process that will own the cloned object (not used in this case).
 * @param obj The boolean object to be cloned.
 * @return A pointer to the original boolean object (since it's a singleton).
 */
static object_t *clone(process_t *process, object_t *obj) {
    return obj;
}

/**
 * @brief Converts the `false` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the string "false".
 */
static string_value_t false_to_string(const object_t *obj) {
    return (string_value_t){ L"false", 5, false };
}

/**
 * @brief Converts the `false` object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the string "false".
 */
static string_value_t false_to_string_notation(const object_t *obj) {
    return false_to_string(obj);
}

/**
 * @brief Converts the `true` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the string "true".
 */
static string_value_t true_to_string(const object_t *obj) {
    return (string_value_t){ L"true", 4, false };
}

/**
 * @brief Converts the `true` object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the string "true".
 */
static string_value_t true_to_string_notation(const object_t *obj) {
    return true_to_string(obj);
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
 * @brief Adds two objects and returns the result.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object.
 * @param obj2 The second object.
 * @return Always returns `false` because addition is not supported for boolean objects.
 */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Subtracts one object from another.
 * @param process Process that will own the resulting object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return Always returns `false` because subtraction is not supported for boolean objects.
 */
static object_t *sub(process_t *process, object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Retrieves the boolean value of the `false` object.
 * @param obj The `false` object.
 * @return Always returns `false`.
 */
static bool false_get_boolean_value(const object_t *obj) {
    return false;
}

/**
 * @brief Retrieves the boolean value of the `true` object.
 * @param obj The `true` object.
 * @return Always returns `true`.
 */
static bool true_get_boolean_value(const object_t *obj) {
    return true;
}

/**
 * @brief Retrieves the integer value of a boolean object.
 * @param obj The boolean object.
 * @return An invalid `int_value_t` indicating that boolean objects cannot be converted
 *  to integers.
 */
static int_value_t get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves the real value of a boolean object.
 * @param obj The boolean object.
 * @return An invalid `real_value_t` indicating that boolean objects cannot be converted
 *  to real numbers.
 */
static real_value_t get_real_value(const object_t *obj) {
    return (real_value_t){ false, 0.0 };
}

/**
 * @var false_vtbl
 * @brief Virtual table defining the behavior of the `false` object.
 */
static object_vtbl_t false_vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = memory_function_stub,
    .dec_ref = memory_function_stub,
    .mark = memory_function_stub,
    .sweep = memory_function_stub,
    .release = memory_function_stub,
    .compare = compare,
    .clone = clone,
    .to_string = false_to_string,
    .to_string_notation = false_to_string_notation,
    .get_property = get_property,
    .set_property = set_property,
    .add = add,
    .sub = sub,
    .get_boolean_value = false_get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
};

/**
 * @brief The singleton instance representing the boolean value `false`.
 */
static object_t false_object = {
    .vtbl = &false_vtbl
};

/**
 * @var true_vtbl
 * @brief Virtual table defining the behavior of the `true` object.
 */
static object_vtbl_t true_vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = memory_function_stub,
    .dec_ref = memory_function_stub,
    .mark = memory_function_stub,
    .sweep = memory_function_stub,
    .release = memory_function_stub,
    .compare = compare,
    .clone = clone,
    .to_string = true_to_string,
    .to_string_notation = true_to_string_notation,
    .get_property = get_property,
    .set_property = set_property,
    .add = add,
    .sub = sub,
    .get_boolean_value = true_get_boolean_value,
    .get_integer_value = get_integer_value,
    .get_real_value = get_real_value
};

/**
 * @brief The singleton instance representing the boolean value `true`.
 */
static object_t true_object = {
    .vtbl = &true_vtbl
};

object_t *get_boolean_object(bool value) {
    return value ? &true_object : &false_object;
}
