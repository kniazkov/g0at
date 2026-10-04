/** @file c_module_output.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Complete C translation units and separate backend diagnostics.
 */
#pragma once
#include "c_module.h"

/** @brief Arena-owned omission report; dependency is set for propagated failures. */
typedef struct c_module_failure_t {
    struct c_module_failure_t *next;
    const c_module_function_t *entry;
    const c_module_function_t *dependency;
    c_generation_status_t status;
    const node_t *failed_node;
} c_module_failure_t;

typedef struct c_module_output_t {
    string_value_t source; /**< Release with FREE_STRING(). */
    size_t generated_count, omitted_count;
    c_module_failure_t *failures;
} c_module_output_t;

/** @brief Emits closed successful dependencies without changing inventory or analysis proofs. */
c_module_output_t generate_c_module(arena_t *arena, const c_module_t *module);
