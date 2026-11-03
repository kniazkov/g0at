/** @file native_runner_policy.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Actual native differential runner policies, independently regression-tested.
 */
#pragma once
#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>

/** Python JSON object semantics: the last duplicate key wins. */
yyjson_val *nr_object_get(yyjson_val *object, const char *key);

bool nr_output_equal(const void *actual,
                     size_t actual_size,
                     const void *expected,
                     size_t expected_size);
/** Strict UTF-8 report decoding; duplicate fields use the last value. */
bool nr_check_report(const void *bytes,
                     size_t size,
                     yyjson_val *case_value,
                     bool native,
                     char *error,
                     size_t error_size);
/** relative is the check path, or name/program.goat. Validate before selection. */
typedef bool (*nr_exists_fn)(const char *relative, bool is_check, void *user);
bool nr_manifest_validate(yyjson_val *cases,
                          nr_exists_fn exists,
                          void *user,
                          char *error,
                          size_t error_size);
typedef bool (*nr_visit_fn)(yyjson_val *case_value, void *user);
/** Select in manifest order, deduplicate names, reject unknowns before any visit.
 * False means invalid selection or >=1 failed callback; failures never stop visits. */
bool nr_run_selected(yyjson_val *cases,
                     const char *const *only,
                     size_t only_count,
                     nr_visit_fn visit,
                     void *user,
                     size_t *total,
                     size_t *failed,
                     char *error,
                     size_t error_size);
