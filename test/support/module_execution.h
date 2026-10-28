/** @file module_execution.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared real-module compile/run fixtures, owned by a scenario.
 */
#pragma once
#include "test/support/execution.h"

typedef struct module_execution_suite_t module_execution_suite_t;
typedef struct module_execution_project_t module_execution_project_t;

typedef struct {
    const char *name, *text;
} module_execution_file_t;

/** @brief All returned objects and strings live until the enclosing scenario finishes. */
module_execution_suite_t *module_execution_suite(scenario_run_t *run, const char *executable);
module_execution_project_t *module_execution_project(module_execution_suite_t *suite,
                                                     const char *name);
const char *module_execution_root(const module_execution_project_t *project);
const char *module_execution_modules(const module_execution_project_t *project);
size_t module_execution_count(const module_execution_suite_t *suite);
const char *module_execution_executable(const module_execution_suite_t *suite);
const char *module_execution_unrelated(const module_execution_suite_t *suite);
const char *const *module_execution_environment(const module_execution_project_t *project);
/** @brief Write UTF-8 text under project root, creating intermediate directories.
 * Relative paths must contain no empty, dot or dot-dot components.
 */
const char *module_execution_put(module_execution_project_t *project,
                                 const char *relative_path,
                                 const char *text);
/** @brief Exact stdout comparison with CRLF normalization, including embedded NUL.
 * Failure means a normal nonzero exit AND nonempty stderr, never a crash/timeout.
 * Environment is inherited per project with GOAT_MODULE_PATH overridden; cwd is
 * the suite's unrelated directory. The parent's cwd/environment never changes.
 */
scenario_command_result_t module_execution_invoke(module_execution_project_t *project,
                                                  const char *mode,
                                                  const char *path,
                                                  const char *output,
                                                  const void *expected,
                                                  size_t expected_size,
                                                  bool fail);
/** @brief Compile source plus optional files and dependencies JSON into a module.
 * NULL version means "1"; NULL dependencies omits the property. A failed compile
 * must not leave an artifact. Returned path is valid even for expected failure.
 */
const char *module_execution_compile(module_execution_project_t *project,
                                     const char *name,
                                     const char *code,
                                     const char *version,
                                     const module_execution_file_t *files,
                                     size_t file_count,
                                     const char *dependencies_json,
                                     bool fail);
const char *module_execution_run(module_execution_project_t *project,
                                 const char *code,
                                 const void *expected,
                                 size_t expected_size);
