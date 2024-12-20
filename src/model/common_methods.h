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
void memory_function_stub(object_t *obj);

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
