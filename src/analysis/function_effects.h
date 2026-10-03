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

/** @brief Dispatches effect collection; missing implementations remain unknown. */
void collect_node_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena);

/** @brief Records an lvalue access, excluding parameters and locals of the analyzed function. */
void record_function_access(function_summary_t *summary,
                            const node_t *node,
                            uint32_t access,
                            arena_t *arena);
