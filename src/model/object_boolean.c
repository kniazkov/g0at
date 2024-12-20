/**
 * @file object_boolean.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of two singleton objects representing the boolean values
 *  `true` and `false`.
 */

#include "object.h"
#include "process.h"
#include "common_methods.h"

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
 * @brief Retrieves all property keys from an object (stub implementation).
 * 
 * This is a stub implementation of the function to retrieve all keys of the properties 
 * defined on an object. Currently, it returns an empty `object_array_t` as a placeholder.
 * 
 * @param obj The object from which to retrieve the keys.
 * @return An empty `object_array_t` (placeholder implementation).
 */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
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
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
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
 * @var false_vtbl
 * @brief Virtual table defining the behavior of the `false` object.
 */
static object_vtbl_t false_vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone,
    .to_string = false_to_string,
    .to_string_notation = false_to_string_notation,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = false_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value
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
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare,
    .clone = clone,
    .to_string = true_to_string,
    .to_string_notation = true_to_string_notation,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = true_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value
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
