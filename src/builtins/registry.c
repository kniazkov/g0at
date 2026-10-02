/** @file registry.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Central registry of native function descriptors.
 */
#include "registry.h"

const builtin_function_t *const *get_builtin_functions(size_t *count) {
    static const builtin_function_t *const functions[] = {&builtin_atan,
                                                          &builtin_print,
                                                          &builtin_sign,
                                                          &builtin_sqrt};
    *count = sizeof(functions) / sizeof(*functions);
    return functions;
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
