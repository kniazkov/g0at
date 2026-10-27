/** @file process_exit.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Atomic exit arbitration and bounded, non-unwinding OS process termination.
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>

/** @brief Zero-initialize before sharing. Zero is open; code+1 atomically publishes a winner. */
typedef struct process_exit_arbiter_t {
    atomic_uint selection;
} process_exit_arbiter_t;

/** @brief Selects a validated 0..255 status exactly once. Invalid codes never reserve it. */
bool process_exit_select(process_exit_arbiter_t *arbiter, unsigned code);
/** @brief Returns -1 before selection, otherwise the selected 0..255 status. */
int process_exit_selected(const process_exit_arbiter_t *arbiter);
/** @brief Starts the process-lifetime watchdog before user/native execution.
 * Idempotent and concurrent-safe. A false result disables flushing, not exit:
 * process_exit_request still terminates immediately with its selected status.
 * Preparation includes a watchdog-ready handshake, avoiding Windows loader-lock
 * dependency when another native thread is in DllMain during a later exit.
 */
bool process_exit_prepare(void);
/** @brief Terminates the whole OS process, not merely one VM/thread.
 * Caller must first validate language arguments and dynamic initializer context.
 * One winner flushes stdout/stderr under one monotonic second; concurrent callers
 * cannot reset the deadline or status. No atexit callbacks, DLL detach callbacks,
 * user finally, native-library destruction or cleanup-service wait is performed.
 * A blocked output write/lock is interrupted by independent process termination.
 * Without successful preparation, terminates immediately instead of risking a wait.
 */
_Noreturn void process_exit_request(unsigned code);
