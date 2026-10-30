/** @file module_loading_owner.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Supervised real loading owner and independent process-identity observations.
 */
#pragma once
#include "test/support/execution.h"
#include "test/support/process_watch.h"
#include "yyjson.h"

typedef struct loading_owner_t loading_owner_t;

typedef struct {
    const void *data;
    size_t size;
} loading_bytes_t;

loading_owner_t *loading_owner_start(scenario_run_t *r,
                                     const char *runner,
                                     const char *root,
                                     const char *name,
                                     const char *archive,
                                     const char *mode,
                                     const char *fault,
                                     const char *cleaner_fault);
void loading_owner_wait_line(loading_owner_t *o, const char *expected);
void loading_owner_resume(loading_owner_t *o);
/** End stdin, require normal exact exit, parse the sole RESULT (NULL for explicit exit).
 * Complete stdout/stderr remain available; explicit exit requires empty stderr.
 */
yyjson_val *loading_owner_end(loading_owner_t *o, int code, bool cleanup, bool explicit_exit);
void loading_owner_expect_output(loading_owner_t *o, const char *expected);
loading_bytes_t loading_owner_output(loading_owner_t *o);
loading_bytes_t loading_owner_error(loading_owner_t *o);
loading_bytes_t loading_owner_calls(loading_owner_t *o);
void loading_owner_expect_calls(loading_owner_t *o, const char *expected);
void loading_owner_wait_calls(loading_owner_t *o, const char *expected);
const char *loading_owner_directory(loading_owner_t *o);
const char *loading_owner_mark(loading_owner_t *o, const char *name);
/** Wait for a populated goat-run-* directory, preserving actual filesystem checks. */
const char *loading_owner_extraction(loading_owner_t *o);
void loading_owner_absent(loading_owner_t *o, const char *path);
void loading_owner_wait_absent(loading_owner_t *o, const char *path);
void loading_owner_require_directory(loading_owner_t *o, const char *path, bool nonempty);
/** These pins are scenario-owned; callers must not close them. */
process_watch_t *loading_owner_watch_cleaner(loading_owner_t *o);
process_watch_t *loading_owner_watch_process(loading_owner_t *o);
void loading_owner_watch_alive(loading_owner_t *o, process_watch_t *watch);
void loading_owner_watch_kill(loading_owner_t *o, process_watch_t *watch);
void loading_owner_watch_wait(loading_owner_t *o, process_watch_t *watch);
/** Forced-owner termination is observed through process_watch, not child status.
 * Afterwards the launcher's wait only reaps/drains and verifies transport cleanup.
 */
void loading_owner_kill_and_reap(loading_owner_t *o);
