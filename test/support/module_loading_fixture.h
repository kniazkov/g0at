/** @file module_loading_fixture.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Scenario-owned real producer/custom native loading fixtures (#369).
 */
#pragma once
#include "test/support/execution.h"
typedef struct loading_fixture_t loading_fixture_t;
loading_fixture_t *loading_fixture_create(scenario_run_t *run,
                                          const char *builder,
                                          const char *compiler,
                                          const char *include_directory,
                                          const char *schema_path);
const char *loading_fixture_root(loading_fixture_t *fixture);
const char *loading_build(loading_fixture_t *fixture,
                          const char *name,
                          const char *source,
                          const char *mode,
                          const char *dependency);
const char *loading_base(loading_fixture_t *fixture);
const char *loading_custom(loading_fixture_t *fixture,
                           const char *name,
                           const char *original,
                           const char *kind,
                           bool nonmatching);
const char *loading_graft(loading_fixture_t *fixture, const char *name, const char *source);
/** plain/foreign/ambiguous/invalid-os are independently rehashed; damaged and
 * foreign-damaged deliberately retain stale supplied hashes. */
const char *loading_transform(loading_fixture_t *fixture,
                              const char *name,
                              const char *original,
                              const char *kind);
