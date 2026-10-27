/** @file scenario.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only scenario lifetime and owned working directory.
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>

typedef struct scenario_context_t scenario_context_t;

typedef struct {
    const char *name;
    const char *build_directory;
    const char *data_directory;
    bool keep_work;
    FILE *diagnostics;
} scenario_options_t;

/** @brief Copy options and create an exclusive directory below an existing build directory.
 * Data and build must be existing, disjoint directories. No data files are modified.
 * Diagnostics defaults to stderr and remains borrowed for the entire context lifetime.
 * This is cooperative test isolation, not a sandbox against concurrent hostile renames.
 */
scenario_context_t *scenario_create(const scenario_options_t *options);
const char *scenario_name(const scenario_context_t *context);
const char *scenario_work_directory(const scenario_context_t *context);
const char *scenario_data_directory(const scenario_context_t *context);
/** @brief Consume context. Preserve failures/keep-work and report their absolute path.
 * Successful runs remove only the owned directory. Changed identity or cleanup failure
 * makes the result false with an explicit diagnostic. Never use the context afterwards.
 */
bool scenario_finish(scenario_context_t *context, bool success);
/** @brief Recognize the exact optional CLI flag; runners reject other unknown arguments. */
bool scenario_keep_work_argument(const char *argument);
