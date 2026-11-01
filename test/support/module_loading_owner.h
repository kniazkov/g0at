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

/** Protocol-free independent owner transport. No descendant adoption or tree kill:
 * the tested cleanup service must remain responsible for its own descendants.
 * Arguments/environment must remain live for start; input is copied (at most 4096 bytes).
 * Paths should be absolute; cwd is inherited. Successful/failing runs retain full captures.
 */
loading_owner_t *loading_owner_command_start(scenario_run_t *r,
                                             const char *root,
                                             const char *name,
                                             const char *const *arguments,
                                             const char *const *environment,
                                             const void *input,
                                             size_t input_size,
                                             bool close_stdin);

/** Independent transport variants for process-primitive tests. Zero means pipe stdin.
 * File input is opened read-only. PTY input is POSIX-only and preserves canonical
 * terminal behavior. new_session is POSIX-only, matching start_new_session=True. */
typedef struct {
    const char *stdin_file;
    bool stdin_pty;
    bool new_session;
} loading_owner_options_t;

loading_owner_t *loading_owner_command_start_options(scenario_run_t *r,
                                                     const char *root,
                                                     const char *name,
                                                     const char *const *arguments,
                                                     const char *const *environment,
                                                     const void *input,
                                                     size_t input_size,
                                                     bool close_stdin,
                                                     const loading_owner_options_t *options);
/** A complete line includes its original newline bytes. False means actual pipe
 * EOF, not merely owner exit. Returned bytes remain valid until the next pump. */
bool loading_owner_next_line(loading_owner_t *o, uint64_t timeout_ms, loading_bytes_t *line);
/** Reap only the owner, independently of descendant-held output pipe EOF. */
void loading_owner_reap(loading_owner_t *o, uint64_t timeout_ms);
void loading_owner_terminate(loading_owner_t *o, bool group_term, uint64_t timeout_ms);
void loading_owner_wait_eof(loading_owner_t *o, uint64_t timeout_ms);
/** Pin only PIDs synchronized at a live readiness barrier. Deferred failure
 * cleanup may signal these handles, never an unpinned or reopened numeric PID. */
void loading_owner_pin_pid(loading_owner_t *o, uint64_t pid);
/** Original existence semantics: POSIX kill(pid,0), including zombies as alive;
 * Windows SYNCHRONIZE handle wait. Unexpected native errors fail closed. */
bool loading_owner_pid_alive(loading_owner_t *o, uint64_t pid);
void loading_owner_pid_gone(loading_owner_t *o, uint64_t pid, uint64_t timeout_ms);
const child_result_t *loading_owner_command_poll(loading_owner_t *o);
void loading_owner_command_wait(loading_owner_t *o,
                                int expected_exit,
                                uint64_t timeout_ms,
                                bool close_stdin);
void loading_owner_command_kill(loading_owner_t *o, uint64_t timeout_ms);

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
