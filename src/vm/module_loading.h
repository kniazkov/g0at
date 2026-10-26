/** @file module_loading.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Run-owned lazy native loading from a fully validated module snapshot.
 */
#pragma once
#include "cli/options.h"
#include "lib/cleanup_service.h"
#include "module/native_environment.h"
#include "module_snapshot.h"

/** Synchronous host diagnostic; borrowed text, no Goat callbacks or reentry. */
typedef void (*module_loading_report_t)(void *state, size_t node, const char *message);
/** Optional host writer, chiefly for bounded I/O integration and fault testing.
 * Must write exactly the supplied bytes on success; may leave a partial file on
 * failure. Never closes the stream. The loader closes and verifies the result
 * before OS loading. No Goat callbacks/reentry. NULL uses write_fs_file.
 */
typedef bool (*module_loading_write_t)(void *state, FILE *file, const void *data, size_t size);

typedef struct {
    native_execution_mode_t mode;
    cleanup_service_t *cleanup; /**< Borrowed shared run service; NULL creates an owned context. */
    const goat_native_environment *environment; /**< Optional pre-detected host facts, copied. */
    module_loading_report_t report;             /**< NULL prints warnings to stderr. */
    void *report_state;
    module_loading_write_t write;
    void *write_state;
} module_loading_options_t;

typedef enum {
    MODULE_LOADING_OK,
    MODULE_LOADING_ARGUMENT,
    MODULE_LOADING_PLATFORM,
    MODULE_LOADING_REQUIRED,
    MODULE_LOADING_BINDING
} module_loading_status_t;

typedef struct {
    module_loading_status_t status;
    size_t node;
    char *diagnostic; /**< Owned with FREE, NULL on success. */
} module_loading_result_t;

/** Exactly once before any execution. Validates platform selection across the
 * complete, already integrity-checked graph, then binds lazy resolvers. Required
 * needs a root-owned compatible specialization, not one from a dependency.
 * Does not create temporary files, start cleaner, or load a library. Off does not
 * select platforms. Platform/required/binding failure forbids execution of this
 * snapshot; invalid API usage does not invalidate an already running snapshot. Input options
 * are copied; shared cleanup/report/writer state must outlive snapshot execution.
 */
module_loading_result_t prepare_module_snapshot_native(module_snapshot_t *snapshot,
                                                       const module_loading_options_t *options);
void clear_module_loading_result(module_loading_result_t *result);

typedef struct {
    size_t extraction_attempts, loader_attempts, warnings;
    size_t bound_functions; /**< Distinct functions in the selected lazy-bound variant. */
    bool loaded, unavailable;
} module_loading_statistics_t;

/** Read-only instrumentation; safe during execution, does not trigger preparation. */
bool module_snapshot_native_statistics(module_snapshot_t *snapshot,
                                       size_t node,
                                       module_loading_statistics_t *out);
/** Borrowed known run path or NULL. A path may survive failed startup and does not prove READY.
 * No I/O; valid until snapshot/shared service destruction.
 */
const char *module_snapshot_native_directory(module_snapshot_t *snapshot);
