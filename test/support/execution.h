/** @file execution.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fail-fast test execution, binary artifacts and a shared monotonic deadline.
 */
#pragma once
#include "lib/process/child_process.h"
#include "test/support/scenario.h"

typedef struct scenario_run_t scenario_run_t;
typedef void (*scenario_body_t)(scenario_run_t *run, void *user);
/** @brief Execute a cooperative body with a mandatory nonzero deadline.
 * Required failures abort the body and run registered cleanup callbacks in reverse order.
 * Bodies and cleanup callbacks must not throw, longjmp, block indefinitely or call exit.
 * Cleanup callbacks must not call this API. Use an externally supervised host for arbitrary
 * test code, crashes and uncooperative hangs. Deadline checks occur at API boundaries and
 * during child pumping; cleanup receives time outside the scenario deadline.
 */
bool scenario_execute(const scenario_options_t *options,
                      uint64_t timeout_ms,
                      scenario_body_t body,
                      void *user);
scenario_context_t *scenario_run_context(scenario_run_t *run);
child_deadline_t scenario_run_deadline(const scenario_run_t *run);
void scenario_checkpoint(scenario_run_t *run);
void scenario_require(scenario_run_t *run, bool condition, const char *message);
/** @brief Register owned cleanup; return true from the callback on success.
 * A false result marks the scenario failed without skipping remaining cleanup callbacks.
 * Registration failure invokes cleanup immediately then fails. The resource must remain
 * valid until scenario_execute returns: use heap storage or caller-owned storage outside
 * the body, never automatic storage in the body or its helpers (failure unwinds them).
 */
void scenario_defer(scenario_run_t *run, bool (*cleanup)(void *), void *resource);
/** @brief Compare explicit byte lengths; optional normalization changes CRLF pairs only.
 * Raw expected/actual bytes are always saved, including NUL and trailing empty lines.
 */
void scenario_require_bytes(scenario_run_t *run,
                            const char *label,
                            const void *expected,
                            size_t expected_size,
                            const void *actual,
                            size_t actual_size,
                            bool normalize_crlf);
/** @brief Run and require a normal expected exit before continuing.
 * Child cwd defaults to the scenario work directory. The shared deadline replaces any
 * command deadline. All output is saved without a bounded-capture truncation limit.
 * Detached/service/result channels and user callbacks are rejected; fixed binary stdin
 * and explicit env are supported. Arguments and results are logged to execution.log.
 */
void scenario_command(scenario_run_t *run, const child_options_t *options, int64_t expected_exit);

/** @brief Full binary command result, owned by the scenario until it finishes. */
typedef struct {
    int64_t exit_code;
    const void *out, *err;
    size_t out_size, err_size;
} scenario_command_result_t;

/** @brief Run a normal child to completion without requiring a particular exit code.
 * Start errors, crashes, timeouts and cleanup errors still fail the scenario.
 * Both complete streams are persisted and returned; no bounded-capture truncation.
 */
scenario_command_result_t scenario_command_capture(scenario_run_t *run,
                                                   const child_options_t *options);
/** @brief Supervise a separate scenario host, requiring exit zero.
 * Prefer this top-level entry for arbitrary test hosts: a crash, exit error or hang becomes
 * failure while the outer process preserves output and stops the owned process tree.
 * This is process lifetime isolation, not a security sandbox. CTest's external timeout must
 * exceed timeout_ms by a cleanup margin so diagnostics and child termination can finish.
 * The caller supplies all host arguments, including any inner workspace/deadline options.
 */
bool scenario_supervise(const scenario_options_t *options,
                        uint64_t timeout_ms,
                        const child_options_t *host);
