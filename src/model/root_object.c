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
