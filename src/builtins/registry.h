/** @file registry.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native library descriptors and name lookup.
 */
#pragma once
#include "model/builtin_function.h"

extern const builtin_function_t builtin_atan;
extern const builtin_function_t builtin_print;
extern const builtin_function_t builtin_sign;
extern const builtin_function_t builtin_sqrt;

/** @brief Enumerates the native function registry. */
const builtin_function_t *const *get_builtin_functions(size_t *count);

/** @brief Looks up a native function by its complete name; constants return NULL. */
const builtin_function_t *find_builtin_function(string_view_t name);
