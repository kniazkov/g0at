/** @file native_builtin.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative identity proofs for native built-in calls.
 */
#pragma once
#include "function_summary.h"
#include "model/builtin_function.h"

/** @brief Resolves a natively-lowered built-in through immutable aliases, or returns NULL. */
const builtin_function_t *resolve_native_builtin(const node_t *expression);
/** @brief Tests a read-only capture against the same whole-program binding proof. */
const builtin_function_t *capture_native_builtin(const function_summary_t *summary,
                                                 const function_capture_t *capture);
