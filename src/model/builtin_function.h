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
    BUILTIN_EFFECT_BINDINGS = 2,
    BUILTIN_EFFECT_INPUT = 4
} builtin_effect_t;

/** @brief Every native function supplies both executors; descriptors are immutable singletons. */
typedef struct builtin_function_t {
    const wchar_t *name;
    size_t min_args;
    unsigned effects;
    /** @brief A concrete normal result on constant arguments proves removable evaluation. */
    bool fold_constants;
    /** @brief Executors receive at least min_args; runtime results own a non-NULL value. */
    operation_result_t (*execute)(object_t **args, uint16_t count, thread_t *thread);
    const lattice_element_t *(*interpret)(abstract_state_t *state,
                                          const lattice_element_t *const *args,
                                          size_t count);
    object_t *(*get_object)(void);
} builtin_function_t;

/** @brief Static storage for a native singleton, owned by its definition file. */
typedef struct {
    object_t base;
    const builtin_function_t *descriptor;
} builtin_function_object_t;

/** @brief Initializes static storage once and returns its stable object identity. */
object_t *get_builtin_function_object(builtin_function_object_t *storage,
                                      const builtin_function_t *descriptor);
