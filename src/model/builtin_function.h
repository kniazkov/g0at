/** @file builtin_function.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared runtime and analysis metadata for native functions.
 */
#pragma once
#include "object.h"

#include <stdint.h>

typedef struct object_t object_t;
typedef struct thread_t thread_t;
typedef struct abstract_state_t abstract_state_t;
typedef struct lattice_element_t lattice_element_t;

/** @brief Observable effects; output alone does not invalidate variable facts. */
typedef enum {
    BUILTIN_EFFECT_NONE = 0,
    BUILTIN_EFFECT_OUTPUT = 1,
    BUILTIN_EFFECT_BINDINGS = 2
} builtin_effect_t;

/** @brief Every native function supplies both executors; descriptors are immutable singletons. */
typedef struct builtin_function_t {
    const wchar_t *name;
    size_t min_args;
    unsigned effects;
    /** @brief Executors receive at least min_args; runtime results own a non-NULL value. */
    operation_result_t (*execute)(object_t **args, uint16_t count, thread_t *thread);
    const lattice_element_t *(*interpret)(abstract_state_t *state,
                                          const lattice_element_t *const *args,
                                          size_t count);
    object_t *(*get_object)(void);
} builtin_function_t;

/** @brief Enumerates the native function registry. */
const builtin_function_t *const *get_builtin_functions(size_t *count);

/** @brief Looks up a native function by its complete name; constants return NULL. */
const builtin_function_t *find_builtin_function(string_view_t name);
