/** @file registry.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native library descriptors and name lookup.
 */
#pragma once
#include "model/builtin_function.h"

extern const builtin_function_t builtin_abs;
object_t *get_function_abs(void);

extern const builtin_function_t builtin_acos;
object_t *get_function_acos(void);

extern const builtin_function_t builtin_asin;
object_t *get_function_asin(void);

extern const builtin_function_t builtin_atan;
object_t *get_function_atan(void);

extern const builtin_function_t builtin_cbrt;
object_t *get_function_cbrt(void);

extern const builtin_function_t builtin_ceil;
object_t *get_function_ceil(void);

extern const builtin_function_t builtin_cos;
object_t *get_function_cos(void);

extern const builtin_function_t builtin_cosh;
object_t *get_function_cosh(void);

extern const builtin_function_t builtin_exp;
object_t *get_function_exp(void);

extern const builtin_function_t builtin_exp2;
object_t *get_function_exp2(void);

extern const builtin_function_t builtin_expm1;
object_t *get_function_expm1(void);

extern const builtin_function_t builtin_floor;
object_t *get_function_floor(void);

extern const builtin_function_t builtin_fmod;
object_t *get_function_fmod(void);

extern const builtin_function_t builtin_hypot;
object_t *get_function_hypot(void);

extern const builtin_function_t builtin_input;
object_t *get_function_input(void);

extern const builtin_function_t builtin_int;
object_t *get_function_int(void);

extern const builtin_function_t builtin_log;
object_t *get_function_log(void);

extern const builtin_function_t builtin_log10;
object_t *get_function_log10(void);

extern const builtin_function_t builtin_log1p;
object_t *get_function_log1p(void);

extern const builtin_function_t builtin_log2;
object_t *get_function_log2(void);

extern const builtin_function_t builtin_max;
object_t *get_function_max(void);

extern const builtin_function_t builtin_min;
object_t *get_function_min(void);

extern const builtin_function_t builtin_pow;
object_t *get_function_pow(void);

extern const builtin_function_t builtin_print;
object_t *get_function_print(void);

extern const builtin_function_t builtin_round;
object_t *get_function_round(void);

extern const builtin_function_t builtin_sign;
object_t *get_function_sign(void);

extern const builtin_function_t builtin_sin;
object_t *get_function_sin(void);

extern const builtin_function_t builtin_sinh;
object_t *get_function_sinh(void);

extern const builtin_function_t builtin_sqrt;
object_t *get_function_sqrt(void);

extern const builtin_function_t builtin_tan;
object_t *get_function_tan(void);

extern const builtin_function_t builtin_tanh;
object_t *get_function_tanh(void);

extern const builtin_function_t builtin_trunc;
object_t *get_function_trunc(void);

/** @brief Enumerates the native function registry. */
const builtin_function_t *const *get_builtin_functions(size_t *count);

/** @brief Looks up a native function by its complete name; constants return NULL. */
const builtin_function_t *find_builtin_function(string_view_t name);

/** @brief Root keys, including native functions, pi and Exceptions; static storage. */
object_array_t get_builtin_context_keys(void);
