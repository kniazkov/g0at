/** @file iteration.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Sound diagnostics for statically known non-iterable sources.
 */
#pragma once
#include "common/compilation_error.h"
void validate_iteration_sources(node_t *root,
                                arena_t *arena,
                                arena_t *errors,
                                compilation_error_t **list);
