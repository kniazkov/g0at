/** @file module_loading_cases.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent noninteractive lazy-loading scenarios (#369).
 */
#pragma once
#include "test/support/module_loading_fixture.h"
size_t loading_normal_case_count(void);
const char *loading_normal_case_name(size_t index);
void loading_normal_case_run(scenario_run_t *run,
                             loading_fixture_t *fixture,
                             const char *runner,
                             size_t index);
