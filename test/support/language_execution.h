/** @file language_execution.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Scenario-owned file and command helpers for language round trips.
 */
#pragma once
#include "test/support/execution.h"
void *language_owned(scenario_run_t *run, void *pointer);
const char *language_join(scenario_run_t *run, const char *directory, const char *name);
const char *language_read(scenario_run_t *run, const char *path, size_t *size);
void language_write(scenario_run_t *run, const char *path, const void *data, size_t size);
void language_directory(scenario_run_t *run, const char *path);
void language_remove(scenario_run_t *run, const char *path);
bool language_exists(scenario_run_t *run, const char *path);
const char *const *language_environment(scenario_run_t *run, bool no_compiler);
scenario_command_result_t language_command(scenario_run_t *run,
                                           const char *exe,
                                           const char *const *arguments,
                                           const char *const *environment);
void language_check(scenario_run_t *run,
                    scenario_command_result_t result,
                    int status,
                    const char *expected,
                    size_t size,
                    const char *error,
                    size_t error_size);

const char *const *
language_environment_override(scenario_run_t *run, const char *name, const char *replacement);
