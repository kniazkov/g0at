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
 * @brief Converts the `false` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the string "false".
 */
static string_value_t false_to_string(object_t *obj) {
    return (string_value_t){ L"false", false };
}

/**
 * @brief Converts the `false` object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the string "false".
 */
static string_value_t false_to_string_notation(object_t *obj) {
    return false_to_string(obj);
}

/**
 * @brief Converts the `true` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` containing the string "true".
 */
static string_value_t true_to_string(object_t *obj) {
    return (string_value_t){ L"true", false };
}

/**
 * @brief Converts the `true` object to a Goat notation string representation.
 * @param obj The object to convert to a Goat notation string.
 * @return A `string_value_t` containing the string "true".
 */
static string_value_t true_to_string_notation(object_t *obj) {
    return true_to_string(obj);
}

/**
 * @brief Adds two objects and returns the result.
 * @param obj1 The first object.
 * @param obj2 The second object.
 * @return Always returns `false` because addition is not supported for boolean objects.
 */
static object_t *add(object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Subtracts one object from another.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return Always returns `false` because subtraction is not supported for boolean objects.
 */
static object_t *sub(object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Retrieves the boolean value of the `false` object.
 * @param obj The `false` object.
 * @return Always returns `false`.
 */
static bool false_get_boolean_value(object_t *obj) {
    return false;
}

/**
 * @brief Retrieves the boolean value of the `true` object.
 * @param obj The `true` object.
 * @return Always returns `true`.
 */
static bool true_get_boolean_value(object_t *obj) {
    return true;
}

/**
 * @brief Retrieves the integer value of a boolean object.
 * @param obj The boolean object.
 * @return An invalid `int_value_t` indicating that boolean objects cannot be converted
 *  to integers.
 */
static int_value_t get_integer_value(object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves the real value of a boolean object.
 * @param obj The boolean object.
 * @return An invalid `real_value_t` indicating that boolean objects cannot be converted
 *  to real numbers.
 */
static real_value_t get_real_value(object_t *obj) {
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
    .to_string = false_to_string,
    .to_string_notation = false_to_string_notation,
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
    .to_string = true_to_string,
    .to_string_notation = true_to_string_notation,
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
