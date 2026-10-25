/** @file registry.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Central registry of native function descriptors.
 */
#include "registry.h"

#define BUILTIN_ENTRY(name) &builtin_##name,
static const builtin_function_t *const functions[] = {GOAT_BUILTIN_FUNCTIONS(BUILTIN_ENTRY)};
#undef BUILTIN_ENTRY

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
