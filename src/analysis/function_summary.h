/** @file function_summary.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Storage for function-body analysis, independent of closure creation and node flags.
 */
#pragma once

#include "lattice.h"

#include <stdint.h>

typedef struct node_t node_t;

/** @brief Analysis progress; inconclusive is not a negative proof. */
typedef enum {
    FUNCTION_UNANALYZED = 0,
    FUNCTION_ANALYZING,
    FUNCTION_ANALYZED,
    FUNCTION_INCONCLUSIVE
} function_analysis_status_t;

/** @brief Independent proof of membership in the supported C subset. */
typedef enum {
    FUNCTION_C_UNKNOWN = 0,
    FUNCTION_C_SUPPORTED,
    FUNCTION_C_UNSUPPORTED
} function_c_support_t;

/** @brief Possible observable body effects; UNKNOWN prevents a purity proof. */
typedef enum {
    FUNCTION_EFFECT_NONE = 0,
    FUNCTION_EFFECT_INPUT = 1,
    FUNCTION_EFFECT_OUTPUT = 2,
    FUNCTION_EFFECT_EXTERNAL_READ = 4,
    FUNCTION_EFFECT_EXTERNAL_WRITE = 8,
    FUNCTION_EFFECT_UNKNOWN = 16
} function_effect_t;

/**
 * @brief Arena-owned record; function and immutable lattice elements are borrowed.
 * Parameter storage belongs to the record's arena. Borrowed data must outlive the record.
 * Status describes the analysis attempt, not precision: ANALYZED may still contain TOP or UNKNOWN.
 * Type signatures do not distinguish closure captures; records are not result caches.
 */
typedef struct function_summary_t {
    struct function_summary_t *next; /**< Next specialization; NULL in snapshots. */
    const node_t *function;
    size_t parameter_count;
    const lattice_element_t **parameter_types; /**< Treat as immutable after registration. */
    const lattice_element_t *return_type;
    uint32_t effects;
    function_analysis_status_t status;
    function_c_support_t c_support;
} function_summary_t;

/** @brief Creates an unanalysed record with TOP types and unknown effects/C support. */
function_summary_t *
create_function_summary(arena_t *arena, const node_t *function, size_t parameter_count);

/** @brief Clears observations, preserving function identity and the parameter type key. */
void reset_function_summary(function_summary_t *summary);

/** @brief Copies mutable record/parameter storage; immutable lattice values remain borrowed. */
const function_summary_t *snapshot_function_summary(arena_t *arena,
                                                    const function_summary_t *summary);

/** @brief Formats status and facts; release the result with FREE_STRING(). */
string_value_t function_summary_to_string(const function_summary_t *summary);

/** @brief Per-function signatures, in first-observation order, owned by the graph arena. */
typedef struct function_summary_set_t {
    arena_t *arena;
    const node_t *function;
    size_t parameter_count;
    function_summary_t *head;
    function_summary_t *tail;
} function_summary_set_t;

/** @brief Starts an empty set; no signature is inferred for an uncalled function. */
function_summary_set_t *
create_function_summary_set(arena_t *arena, const node_t *function, size_t parameter_count);

/** @brief Drops current signatures; arena-owned records and event snapshots remain valid. */
void reset_function_summary_set(function_summary_set_t *set);

/**
 * @brief Finds or adds a type signature; missing arguments are NULL, extras are ignored.
 * Constants/ranges lose their values, callable identities become FUNCTION, typed arrays ARRAY.
 * BOTTOM in any supplied argument prevents registration, including ignored extra arguments.
 */
function_summary_t *register_function_specialization(function_summary_set_t *set,
                                                     const lattice_element_t *const *args,
                                                     size_t count);
