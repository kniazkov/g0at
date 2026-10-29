/** @file module_snapshot_fixture.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent snapshot/identity archive constructor; no production writers.
 */
#pragma once
#include "test/support/format_binary.h"
#include "test/support/language_execution.h"

typedef struct {
    const char *record, *path;
    const void *data;
    size_t size;
} snapshot_fixture_t;

/** Scenario-owned result; schema cache must start NULL and belongs to this scenario.
 * Literal fixture inputs only: 1..11 files, label/dormant <=64 UTF-8 bytes. */
snapshot_fixture_t snapshot_fixture_archive(scenario_run_t *r,
                                            const char *vectors,
                                            yyjson_mut_doc **schema,
                                            const char *name,
                                            const char *version,
                                            const char *deps,
                                            int value,
                                            const char *label,
                                            unsigned files,
                                            bool throwing,
                                            bool native,
                                            const char *dormant);
