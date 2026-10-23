/** @file registry.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Central registry of native function descriptors.
 */
#include "registry.h"

static const builtin_function_t *const functions[] = {
    &builtin_abs,           &builtin_acos,          &builtin_asin,    &builtin_atan,
    &builtin_cbrt,          &builtin_ceil,          &builtin_char_at, &builtin_chr,
    &builtin_contains,      &builtin_cos,           &builtin_cosh,    &builtin_ends_with,
    &builtin_exp,           &builtin_exp2,          &builtin_expm1,   &builtin_floor,
    &builtin_fmod,          &builtin_format,        &builtin_hypot,   &builtin_index_of,
    &builtin_input,         &builtin_instance_of,   &builtin_int,     &builtin_is_blank,
    &builtin_is_empty,      &builtin_last_index_of, &builtin_log,     &builtin_log10,
    &builtin_log1p,         &builtin_log2,          &builtin_max,     &builtin_min,
    &builtin_ord,           &builtin_pow,           &builtin_print,   &builtin_println,
    &builtin_repeat,        &builtin_replace,       &builtin_round,   &builtin_sign,
    &builtin_sin,           &builtin_sinh,          &builtin_sqrt,    &builtin_starts_with,
    &builtin_str,           &builtin_substring,     &builtin_tan,     &builtin_tanh,
    &builtin_to_lower_case, &builtin_to_upper_case, &builtin_trim,    &builtin_trunc};

const builtin_function_t *const *get_builtin_functions(size_t *count) {
    *count = sizeof(functions) / sizeof(*functions);
    return functions;
}

object_array_t get_builtin_context_keys(void) {
    enum { count = sizeof(functions) / sizeof(*functions) };

    static object_static_string_t names[count];
    static object_t *keys[count + 2];
    if (!keys[0]) {
        keys[0] = get_string_exceptions();
        keys[1] = get_string_pi();
        for (size_t i = 0; i < count; i++)
            keys[i + 2] = get_static_string_object(&names[i], functions[i]->name);
    }
    return (object_array_t){keys, count + 2};
}

const builtin_function_t *find_builtin_function(string_view_t name) {
    size_t count;
    const builtin_function_t *const *functions = get_builtin_functions(&count);
    for (size_t i = 0; i < count; i++) {
        if (STRLEN(functions[i]->name) == name.length
            && !MEMCMP(functions[i]->name, name.data, name.length))
            return functions[i];
    }
    return NULL;
}
