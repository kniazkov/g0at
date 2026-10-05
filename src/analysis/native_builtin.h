/** @file native_builtin.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative identity proofs for native built-in calls.
 */
#pragma once
#include "function_summary.h"

/** @brief Resolves abs through immutable aliases only when the program never writes it. */
bool resolve_native_abs(const node_t *expression);
/** @brief Tests a read-only capture against the same whole-program binding proof. */
bool native_abs_capture(const function_summary_t *summary, const function_capture_t *capture);
