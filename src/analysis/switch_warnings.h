/** @file switch_warnings.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Non-blocking diagnostics for provably duplicate case values.
 */
#pragma once
#include "common/compilation_error.h"

/** @brief Visits switch nodes and compares closed scalar labels using the lattice. */
void warn_duplicate_cases(node_t *root,
                          arena_t *arena,
                          arena_t *errors,
                          compilation_error_t **list);
