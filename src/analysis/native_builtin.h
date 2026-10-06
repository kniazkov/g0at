/** @file native_builtin.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative identity proofs for native built-in calls.
 */
#pragma once
#include "function_summary.h"

/** @brief Native built-ins that the C generator lowers directly. */
typedef enum {
    NATIVE_BUILTIN_NONE = 0,
    NATIVE_BUILTIN_ABS,
    NATIVE_BUILTIN_ATAN
} native_builtin_kind_t;

/** @brief Resolves a native built-in through immutable aliases, or returns NONE. */
native_builtin_kind_t resolve_native_builtin(const node_t *expression);
/** @brief Tests a read-only capture against the same whole-program binding proof. */
native_builtin_kind_t capture_native_builtin(const function_summary_t *summary,
                                             const function_capture_t *capture);
