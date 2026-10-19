/** @file array_methods.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Array-scoped method registration; these names are not global builtins.
 */
#pragma once
#include "model/object.h"

/** @brief Borrowed static keys of Array's methods. */
object_array_t get_array_method_keys(void);
/** @brief Finds a built-in Array method for the supplied property key. */
object_t *get_array_method(const object_t *key);
