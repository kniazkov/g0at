/**
 * @file common_methods.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Declarations of common methods for the Goat objects.
 * 
 * This file contains declarations of methods that are shared across multiple 
 * object implementations in the Goat language. These methods are abstracted into 
 * a separate file to promote code reuse and maintainability.
 */

#pragma once

#include "object.h"

/**
 * @brief A stub memory management function for singleton objects.
 * @param obj The object. This parameter is unused.
 * @note This function does nothing because singleton objects do not require memory management.
 */
void stub_memory_function(object_t *obj);

/**
 * @brief Compares the memory addresses of two objects.
 * 
 * This function compares the memory addresses of two objects. The comparison is based solely on
 * their addresses, not their contents. It is designed to be used as a comparator for various
 * object types, including user-defined objects, ensuring consistent ordering in structures like
 * AVL trees.
 * 
 * @param obj1 The first object to compare.
 * @param obj2 The second object to compare.
 * @return An integer indicating the result of the comparison:
 *         - 1 if obj1 > obj2,
 *         - -1 if obj1 < obj2,
 *         - 0 if obj1 == obj2.
 */
int compare_object_addresses(const object_t *obj1, const object_t *obj2);

/**
 * @brief Implementation of the `clone` method for singleton objects.
 * 
 * This method returns the same singleton object without creating a new instance. 
 * Since singletons are unique and immutable in this context, cloning simply means returning
 * the original object.
 * 
 * @param process The current process context (unused in this implementation).
 * @param obj The singleton object to be cloned.
 * @return A pointer to the same singleton object (`obj`).
 */
object_t *clone_singleton(process_t *process, object_t *obj);

/**
 * @brief Converts an object to its string representation.
 * 
 * This implementation uses the `to_string_notation` method as a fallback for generating 
 * the string representation of an object. It is suitable for general-purpose use where 
 * no specialized `to_string` behavior is required.
 * 
 * @param obj The object to convert to a string.
 * @return The string representation of the object as a `string_value_t`.
 */
string_value_t common_to_string(const object_t *obj);

/**
 * @brief Converts an object to its Goat notation string representation.
 * 
 * This implementation generates a string representation in Goat notation for objects. 
 * It iterates over the keys of the object, retrieves each key-value pair, and constructs 
 * a string in the format `{key1=value1;key2=value2;...}`. The keys and values are converted 
 * to their own string representations using their respective `to_string_notation` methods.
 * 
 * @param obj The object to convert to Goat notation.
 * @return The Goat notation string representation of the object as a `string_value_t`.
 * @note The memory for the returned string is dynamically allocated. The caller is responsible
 *  for freeing the memory after use.
 */
string_value_t common_to_string_notation(const object_t *obj);

/**
 * @brief Stub for setting a property on an immutable object.
 * 
 * This function attempts to set a property on an immutable object, such as a singleton. 
 * Since immutable objects cannot have their properties added or modified, the function 
 * always fails and returns `false`.
 * 
 * @param obj The immutable object on which the property setting was attempted.
 * @param key The key of the property to set.
 * @param value The value to assign to the property.
 * @return Always returns `false`, indicating that the operation is not supported.
 */
bool set_property_on_immutable(object_t *obj, object_t *key, object_t *value);

/**
 * @brief Stub implementation for the `add` operation on unsupported objects.
 * 
 * This function serves as a placeholder for the `add` operation. It is used when an object does
 * not support addition. The function always returns `NULL`, indicating that the operation is not
 * supported.
 * 
 * @param process The current process context (unused in this implementation).
 * @param obj1 The first object to add.
 * @param obj2 The second object to add.
 * @return Always returns `NULL`, indicating the operation is unsupported.
 */
object_t *stub_add(process_t *process, object_t *obj1, object_t *obj2);

/**
 * @brief Stub implementation for the `sub` operation on unsupported objects.
 * 
 * This function serves as a placeholder for the `sub` operation. It is used when an object does
 * not support subtraction. The function always returns `NULL`, indicating that the operation is
 * not supported.
 * 
 * @param process The current process context (unused in this implementation).
 * @param obj1 The first object (minuend).
 * @param obj2 The second object (subtrahend).
 * @return Always returns `NULL`, indicating the operation is unsupported.
 */
object_t *stub_sub(process_t *process, object_t *obj1, object_t *obj2);

/**
 * @brief Retrieves the boolean value of an object based on its keys.
 * 
 * This function provides a common implementation for the `get_boolean_value` method. 
 * An object is considered `true` if it has at least one key; otherwise, it is `false`. 
 * 
 * @param obj The object to evaluate.
 * @return `true` if the object has one or more keys, `false` otherwise.
 */
bool common_get_boolean_value(const object_t *obj);

/**
 * @brief Stub implementation for retrieving the integer value of an object.
 * 
 * This function serves as a placeholder for the `get_integer_value` method in cases where 
 * an object does not support conversion to an integer. The function always returns an 
 * `int_value_t` with `has_value` set to `false`.
 * 
 * @param obj The object from which to attempt to retrieve the integer value.
 * @return An `int_value_t` structure with `has_value` set to `false`.
 */
int_value_t stub_get_integer_value(const object_t *obj);

/**
 * @brief Stub implementation for retrieving the floating-point value of an object.
 * 
 * This function serves as a placeholder for the `get_real_value` method in cases where 
 * an object does not support conversion to a floating-point value. The function always 
 * returns a `real_value_t` with `has_value` set to `false`.
 * 
 * @param obj The object from which to attempt to retrieve the floating-point value.
 * @return A `real_value_t` structure with `has_value` set to `false`.
 */
real_value_t stub_get_real_value(const object_t *obj);
