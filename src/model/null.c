/**
 * @file null.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of the `null` object in the Goat programming language.
 * 
 * This file defines the `null` object, which represents a special constant value in Goat.
 * Unlike typical null values, which may be considered the absence of an object, in Goat,
 * `null` is treated as a fully-fledged object with predefined behavior. This design ensures
 * consistency with the language's object-oriented philosophy, where even constants like `null`
 * adhere to the same object principles as other entities.
 * 
 * The `null` object is implemented as a singleton, ensuring there is only one instance of it
 * in the program. This object has a virtual table (`vtbl`) that defines its behavior, including
 * how it is represented as a string, how properties are accessed (stubbed as not implemented),
 * and other object-specific methods.
 * 
 * The `null` object is used throughout the Goat language in situations where an object is
 * expected but no meaningful value is available. It can be assigned to variables, passed as
 * arguments, and returned from functions. Since `null` is an object, it can participate in
 * operations like comparison and string conversion, even though its behavior is predefined
 * and limited.
 * 
 * The `null` object has the following behavior:
 * - It is always represented as the string `"null"`.
 * - It evaluates to `false` when used in boolean contexts.
 * - It does not support modification, as its properties are immutable.
 * - Any attempt to perform operations such as addition or subtraction on `null` will result
 *   in an exception being thrown, as these operations are not valid for the `null` object.
 * - Some properties of `null` are still accessible, for example, the `instanceOf` property will
 *   work and correctly return the appropriate prototype information, as `null` is still an object
 *   in Goat's object-oriented model.
 * 
 * This object is critical for representing the absence of a value in Goat's object-oriented model,
 * ensuring that all values are treated as objects, even in cases where there is no actual data.
 */

#include "object.h"
#include "process.h"
#include "common_methods.h"

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
 * @brief Converts the `null` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the string "null".
 */
static string_value_t to_string(const object_t *obj) {
    return (string_value_t){ L"null", 4, false };
}

/**
 * @brief Converts the `null` object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the string "null".
 */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/**
 * @brief Retrieves the boolean value of the `null` object.
 * @param obj The `null` object.
 * @return Always returns `false`.
 */
static bool get_boolean_value(const object_t *obj) {
    return false;
}

/**
 * @var vtbl
 * @brief Virtual table defining the behavior of the `null` object.
 */
static object_vtbl_t vtbl = {
    .type = TYPE_OTHER,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = no_sweep,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = to_string,
    .to_string_notation = to_string_notation,
    .get_prototypes = common_get_prototypes,
    .get_topology = common_get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .create_property = create_property_on_immutable,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .subtract = stub_subtract,
    .multiply = stub_multiply,
    .divide = stub_divide,
    .modulo = stub_modulo,
    .power = stub_power,
    .less = common_less,
    .less_or_equal = common_less_or_equal,
    .greater = common_greater,
    .greater_or_equal = common_greater_or_equal,
    .equal = common_equal,
    .not_equal = common_not_equal,
    .get_boolean_value = get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @brief The singleton instance representing the `null` object.
 */
static object_t null_object = {
    .vtbl = &vtbl
};

object_t *get_null_object() {
    return &null_object;
}
