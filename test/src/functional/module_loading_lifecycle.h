/** @file module_loading_lifecycle.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Retained snapshots and real owner/cleaner lifecycle scenarios (#369).
 */
#pragma once
#include "test/support/module_loading_fixture.h"
size_t loading_lifecycle_case_count(void);
const char *loading_lifecycle_case_name(size_t index);
void loading_lifecycle_case_run(scenario_run_t *run,
                                loading_fixture_t *fixture,
                                const char *runner,
                                size_t index);
