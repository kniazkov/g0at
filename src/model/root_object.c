/**
 * @file root_object.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the root singleton object in the Goat programming language.
 * 
 * This file implements the root object in Goat, which is the ultimate prototype 
 * in the prototype chain. It serves as the foundation for all other objects, 
 * with every object in the language having this object as the last in its prototype 
 * chain. The root object has no prototype itself, and its topology is empty.
 * It contains the methods and fields that all objects in Goat share.
 */

#include "object.h"
#include "common_methods.h"

/**
 * @brief Retrieves the prototypes of the root object.
 * 
 * This function returns an empty array of prototypes because the root object
 * does not have any prototypes.
 * 
 * @param obj The object whose prototypes are to be retrieved.
 * @return An object_array_t containing zero prototypes.
 */
static object_array_t get_prototypes(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
}

/**
 * @brief Retrieves the full prototype topology of the root object.
 * 
 * This function returns an empty array because the root object does not have
 * any prototypes in its inheritance chain.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing zero prototypes in the chain.
 */
static object_array_t get_topology(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
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
 * @var vtbl
 * @brief Virtual table defining the behavior of the root object.
 */
static object_vtbl_t vtbl = {
    .type = TYPE_BOOLEAN,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = stub_memory_function,
    .release = stub_memory_function,
    .compare = compare_object_addresses,
    .clone = clone_singleton,
    .to_string = common_to_string,
    .to_string_notation = common_to_string_notation,
    .get_prototypes = get_prototypes,
    .get_topology = get_topology,
    .get_keys = get_keys,
    .get_property = get_property,
    .add_property = add_property_on_immutable,
    .set_property = set_property_on_immutable,
    .add = stub_add,
    .sub = stub_sub,
    .get_boolean_value = common_get_boolean_value,
    .get_integer_value = stub_get_integer_value,
    .get_real_value = stub_get_real_value
};

/**
 * @brief The singleton instance of root object.
 */
static object_t root_object = {
    .vtbl = &vtbl
};

object_t *get_root_object() {
    return &root_object;
}
