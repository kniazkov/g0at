/**
 * @file object_boolean.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of two singleton objects representing boolean values: 'true' and 'false'.
 */

#include "object.h"
#include "process.h"

/**
 * @brief ....
 * @param obj .....
 * @note This function does nothing because the boolean object is a singleton.
 */
static void memory_function_stub(object_t *obj) {
}

/**
 * @brief Converts `false` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` structure containing the string representation of the object.
 */
static string_value_t false_to_string(object_t *obj) {
    return (string_value_t){ L"false", false };
}

/**
 * @brief Converts `false` object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 */
static string_value_t false_to_string_notation(object_t *obj) {
    return false_to_string(obj);
}

/**
 * @brief Converts `true` object to a string representation.
 * @param obj The object to convert to a string.
 * @return A `string_value_t` structure containing the string representation of the object.
 */
static string_value_t true_to_string(object_t *obj) {
    return (string_value_t){ L"true", false };
}

/**
 * @brief Converts `true` object to a Goat notation string representation.
 * @param obj The object to convert to a string in Goat notation.
 * @return A `string_value_t` structure containing the Goat notation string representation.
 */
static string_value_t true_to_string_notation(object_t *obj) {
    return true_to_string(obj);
}


/**
 * @brief Adds two objects and returns the result as a new object.
 * @param obj1 The first object to add.
 * @param obj2 The second object to add.
 * @return 'false` because operation is not supported.
 */
static object_t *add(object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Subtracts the value of the second object from the first object.
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return 'false` because operation is not supported.
 */
static object_t *sub(object_t *obj1, object_t *obj2) {
    return false;
}

/**
 * @brief Retrieves the boolean representation of the `false` object.
 * @param obj The object from which to retrieve the boolean value.
 * @return Boolean representation of the object.
 */
static bool false_get_boolean_value(object_t *obj) {
    return false;
}

/**
 * @brief Retrieves the boolean representation of the `true` object.
 * @param obj The object from which to retrieve the boolean value.
 * @return Boolean representation of the object.
 */
static bool true_get_boolean_value(object_t *obj) {
    return true;
}

/**
 * @brief Retrieves the integer value of a boolean object.
 * @param obj The object from which to retrieve the integer value.
 * @return Invalid value i.e. boolean can't be converted to integer
 */
static int_value_t get_integer_value(object_t *obj) {
    return (int_value_t){ false, 0 };
}

/**
 * @brief Retrieves the real value of a boolean object.
 * @param obj The object from which to retrieve the real value.
 * @return Invalid value i.e. boolean can't be converted to real.
 */
static real_value_t get_real_value(object_t *obj) {
    return (real_value_t){ false, 0.0 };
}

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the `false` object.
 */
static object_vtbl_t false_vtbl = {
    .type = TYPE_NUMBER,
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
 * @brief `false` object singleton
 */
static object_t false_object = {
    .vtbl = &false_vtbl
};

/**
 * @var vtbl
 * @brief This virtual table defines the behavior of the `true` object.
 */
static object_vtbl_t true_vtbl = {
    .type = TYPE_NUMBER,
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
 * @brief `true` object singleton
 */
static object_t true_object = {
    .vtbl = &true_vtbl
};

object_t *get_boolean_object(bool value) {
    return value ? &true_object : &false_object;
}
