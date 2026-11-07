/** @file module_artifact.h
 * @brief Test-only ZIP artifacts assembled with independent format oracles.
 */
#pragma once
#include "language_execution.h"
const unsigned char *test_module_member(scenario_run_t *run,
                                        const void *zip,
                                        size_t size,
                                        const char *name,
                                        size_t *length);
const unsigned char *
test_module_wrap_image(scenario_run_t *run, const void *image, size_t size, size_t *length);
/** Replace one member; optionally update that native variant's SHA-256 only.
 * ZIP CRCs are always rebuilt, so corruptions reach semantic validation. */
const unsigned char *test_module_replace(scenario_run_t *run,
                                         const void *zip,
                                         size_t size,
                                         const char *name,
                                         const void *data,
                                         size_t data_size,
                                         bool native_hash,
                                         size_t *length);
