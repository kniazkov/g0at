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
 * @var boolean_proto_vtbl
 * @brief Virtual table defining the behavior of the boolean prototype object.
 */
static object_vtbl_t boolean_proto_vtbl = {
    .type = TYPE_STRING,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = common_to_string,
    .to_string_notation = common_to_string_notation,
    .get_prototypes = common_get_prototypes,
    .get_topology = common_get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = stub_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
};

/**
 * @var boolean_proto
 * @brief The boolean prototype object.
 * 
 * This is the boolean prototype object, which is the instance that serves as the 
 * prototype for all boolean objects.
 */
static object_t boolean_proto = {
    .vtbl = &boolean_proto_vtbl
};

object_t *get_boolean_proto() {
    return &boolean_proto;
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
 * @var prototypes
 * @brief Array of prototypes for the boolean object.
 * 
 * It contains only the `boolean_proto` prototype.
 */
static object_t* prototypes[] = {
    &boolean_proto
};

/**
 * @brief Retrieves the prototypes of a boolean object.
 * 
 * This function returns an array of prototypes for a boolean object.
 * In this case, it contains only the boolean prototype.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing the prototypes of the boolean object.
 */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/**
 * @brief Retrieves the full prototype topology of a boolean object.
 * 
 * This function returns the full prototype chain (topology) of a boolean object.
 * The topology includes the `boolean_proto` prototype and the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the full prototype chain.
 */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &boolean_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
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
    .clone = clone_singleton,
    .to_string = false_to_string,
    .to_string_notation = false_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = false_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
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
    .clone = clone_singleton,
    .to_string = true_to_string,
    .to_string_notation = true_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = true_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value,
    .call = stub_call
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
