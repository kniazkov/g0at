/** @file registry.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Central registry of native function descriptors.
 */
#include "registry.h"

static const builtin_function_t *const functions[] = {
    &builtin_abs,   &builtin_acos,  &builtin_asin,  &builtin_atan, &builtin_cbrt,  &builtin_ceil,
    &builtin_cos,   &builtin_cosh,  &builtin_exp,   &builtin_exp2, &builtin_expm1, &builtin_floor,
    &builtin_fmod,  &builtin_hypot, &builtin_input, &builtin_int,  &builtin_log,   &builtin_log10,
    &builtin_log1p, &builtin_log2,  &builtin_max,   &builtin_min,  &builtin_pow,   &builtin_print,
    &builtin_round, &builtin_sign,  &builtin_sin,   &builtin_sinh, &builtin_sqrt,  &builtin_tan,
    &builtin_tanh,  &builtin_trunc};

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
        if (wcslen(functions[i]->name) == name.length
            && !wmemcmp(functions[i]->name, name.data, name.length))
            return functions[i];
    }
    return NULL;
}
