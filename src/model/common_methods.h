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

object_t *clone_singleton(process_t *process, object_t *obj);

string_value_t common_to_string(const object_t *obj);

string_value_t common_to_string_notation(const object_t *obj);

/**
 * @brief Attempts to set a property on an immutable object.
 * 
 * This function is used to set a property on an immutable object (for example, on a sigleton).
 * However, since the object is immutable, no property can be added or modified. Therefore, the
 * function always returns `false`.
 * 
 * @param obj The immutable object on which to set the property.
 * @param key The key of the property to set.
 * @param value The value to assign to the property.
 * @return Always returns `false` because the object is immutable.
 */
bool set_property_on_immutable(object_t *obj, object_t *key, object_t *value);

object_t *stub_add(process_t *process, object_t *obj1, object_t *obj2);

object_t *stub_sub(process_t *process, object_t *obj1, object_t *obj2);

bool common_get_boolean_value(const object_t *obj);

int_value_t stub_get_integer_value(const object_t *obj);

real_value_t stub_get_real_value(const object_t *obj);
