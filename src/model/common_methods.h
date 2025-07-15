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
 * @brief A stub sweep function for objects that should never be swept (like singletons).
 * @param obj The object. This parameter is unused.
 * @return Always returns false since the object should never be collected.
 * @note This function is used for permanent objects that should never participate
 *       in garbage collection (e.g., singletons, global constants).
 */
bool no_sweep(object_t *obj);

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
 * @brief Common function to retrieve prototypes for most objects.
 * 
 * This function returns an array containing a single object — the root object.
 * It serves as a common implementation for both `get_prototypes` and `get_topology` methods.
 * This will work for most objects, where only the root object is part of their prototype chain.
 * 
 * @param obj The object whose prototypes or topology are to be retrieved.
 * @return An object_array_t containing the root object.
 */
object_array_t common_get_prototypes(const object_t *obj);

/**
 * @brief Common function to retrieve the full prototype topology for most objects.
 * 
 * This function returns the same result as `common_get_prototypes` by providing
 * an array containing a single object — the root object.
 * 
 * @param obj The object whose prototype topology is to be retrieved.
 * @return An object_array_t containing the root object, representing the full prototype topology.
 */
object_array_t common_get_topology(const object_t *obj);

/**
 * @brief Stub for adding a property to an immutable object.
 * 
 * This function attempts to add a property to an immutable object, such as a singleton.
 * Since immutable objects cannot have new properties added or existing ones modified,
 * the function always fails and returns `MSTAT_IMMUTABLE_OBJECT`.
 * 
 * @param obj The immutable object on which the property additionMSTAT_IMMUTABLE_OBJECT was attempted.
 * @param key The key of the property to add.
 * @param value The value to assign to the property.
 * @param constant Whether the property should be constant (ignored for immutable objects).
 * @return Always returns `MSTAT_IMMUTABLE_OBJECT`, indicating that the operation is not supported.
 */
model_status_t add_property_on_immutable(object_t *obj, object_t *key, object_t *value, bool constant);

/**
 * @brief Stub for setting a property on an immutable object.
 * 
 * This function attempts to set a property on an immutable object, such as a singleton. 
 * Since immutable objects cannot have their properties added or modified, the function 
 * always fails and returns `MSTAT_IMMUTABLE_OBJECT`.
 * 
 * @param obj The immutable object on which the property setting was attempted.
 * @param key The key of the property to set.
 * @param value The value to assign to the property.
 * @return Always returns `MSTAT_IMMUTABLE_OBJECT`, indicating that the operation is not supported.
 */
model_status_t set_property_on_immutable(object_t *obj, object_t *key, object_t *value);

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
 * @brief Stub implementation for retrieving the boolean value of an object.
 *
 * This function is the default implementation of the `get_boolean_value` method.
 * By default, any object is considered a "some entity" and is interpreted as `true` in boolean
 * contexts, since it exists and holds a valid state.
 *
 * @param obj Pointer to the object being evaluated.
 * @return `true`, indicating the object exists and is considered truthy.
 */
bool stub_get_boolean_value(const object_t *obj);

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

/**
 * @brief Stub implementation of the `call` function for non-functional objects.
 *
 * This function acts as a placeholder for objects that do not support the `call` operation.
 * It always returns `false`, indicating that the object is not a functional object.
 *
 * @param obj Pointer to the object being invoked.
 * @param arg_count The number of arguments passed to the function (ignored in this implementation).
 * @param thread Pointer to the thread in which the function was supposed to be executed (ignored).
 * @return `false`, as the object is not a functional object.
 */
bool stub_call(object_t *obj, uint16_t arg_count, thread_t *thread);