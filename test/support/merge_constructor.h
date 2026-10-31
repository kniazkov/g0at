/** @file merge_constructor.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Real-platform no-load constructor fixture for merge scenarios.
 */
#pragma once
#include "test/support/execution.h"
#include "yyjson.h"
typedef struct merge_constructor_t merge_constructor_t;
merge_constructor_t *merge_constructor_prepare(scenario_run_t *run,
                                               const char *compiler,
                                               const char *data_directory,
                                               const char *root,
                                               const char *format_directory,
                                               yyjson_mut_val *record);
void merge_constructor_verify(merge_constructor_t *fixture);
