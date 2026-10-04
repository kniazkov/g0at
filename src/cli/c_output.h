/** @file c_output.h
 * @copyright 2026 Ivan Kniazkov
 * @brief C source and shared-library export.
 */
#pragma once
#include "graph/node.h"
#include "options.h"

/** @brief Replaces the input extension; NULL input uses generated.c, C input is rejected. */
path_t *c_output_path(const path_t *input);
/** @brief Exports a C module/library and reports omissions without running Goat code. */
bool output_c_module(const options_t *options, arena_t *arena, const node_t *root);

/** @brief Changes the basename extension; NULL prevents overwriting identically named input. */
path_t *output_path(const path_t *input, const char *extension);
