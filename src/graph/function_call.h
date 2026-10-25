/** @file function_call.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative diagnostics recorded by module interface interpretation.
 */
#pragma once
#include "node.h"
/** True only for a top-level call whose callee was always a proven scalar.
 * The caller emits a warning, not an error, preserving the existing runtime outcome. */
bool module_call_is_noncallable(const node_t *node);
