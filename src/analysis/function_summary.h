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
 * This initial record has no specialization key and is not consumed by optimization.
 */
typedef struct function_summary_t {
    const node_t *function;
    size_t parameter_count;
    const lattice_element_t **parameter_types;
    const lattice_element_t *return_type;
    uint32_t effects;
    function_analysis_status_t status;
    function_c_support_t c_support;
} function_summary_t;

/** @brief Creates an unanalysed record with TOP types and unknown effects/C support. */
function_summary_t *
create_function_summary(arena_t *arena, const node_t *function, size_t parameter_count);

/** @brief Clears observations, preserving function identity and parameter storage. */
void reset_function_summary(function_summary_t *summary);

/** @brief Copies mutable record/parameter storage; immutable lattice values remain borrowed. */
const function_summary_t *snapshot_function_summary(arena_t *arena,
                                                    const function_summary_t *summary);

/** @brief Formats status and facts; release the result with FREE_STRING(). */
string_value_t function_summary_to_string(const function_summary_t *summary);
