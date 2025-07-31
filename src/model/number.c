/**
 * @file number.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of a prototype for numeric objects (integer and float).
 *
 * This file defines the behavior of numeric objects, which include both
 * integer and floating-point numbers. The numeric objects in this system follow a 
 * prototype-based inheritance model, where a common prototype is used for all numeric objects.
 */

#include "object.h"
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
 * @var numeric_proto_vtbl
 * @brief Virtual table defining the behavior of the numeric prototype object.
 */
static object_vtbl_t numeric_proto_vtbl = {
    .type = TYPE_OTHER,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = no_sweep,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = common_to_string,
    .to_string_notation = common_to_string_notation,
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
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @var numeric_proto
 * @brief The numeric prototype object.
 * 
 * This is the numeric prototype object, which is the instance that serves as the 
 * prototype for all numeric objects.
 */
static object_t numeric_proto = {
    .vtbl = &numeric_proto_vtbl
};

object_t *get_numeric_proto() {
    return &numeric_proto;
}
