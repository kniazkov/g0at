/** @file function_effects.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Direct body effects and lexical captures.
 */
#pragma once

#include "function_summary.h"

/** @brief Scans all branches of registered bodies; nested bodies are analyzed separately. */
void analyze_function_direct_effects(node_t *root);

/** @brief Looks up a captured binding by declaration identity and name. */
const function_capture_t *find_function_capture(const function_summary_t *summary,
                                                const declarator_t *declarator,
                                                string_view_t name);
