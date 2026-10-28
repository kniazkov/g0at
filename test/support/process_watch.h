/** @file process_watch.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent OS process identity and condition observation for tests.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct process_watch_t process_watch_t;

typedef enum {
    PROCESS_WATCH_ERROR = -1,
    PROCESS_WATCH_TIMEOUT = 0,
    PROCESS_WATCH_OK = 1
} process_watch_result_t;

/** @brief Pin an existing process identity, never fall back to signalling a PID.
 * Caller must synchronize with the process before opening (for example a ready
 * pipe while the process waits for release); a bare PID cannot establish its
 * identity before this call. Failures return NULL and a native errno/GetLastError.
 * Linux requires pidfd_open and pidfd_send_signal; unsupported kernels fail
 * explicitly. The observer does not launch, reap, or own the observed process.
 */
process_watch_t *process_watch_open(uint64_t pid, unsigned long *error);
process_watch_result_t
process_watch_alive(process_watch_t *watch, bool *alive, unsigned long *error);
/** @brief Wait for OS exit notification until an absolute monotonic deadline.
 * A dead process succeeds even at an expired deadline. TIMEOUT is not an error.
 * Does not reap child processes; their launcher retains that responsibility.
 */
process_watch_result_t
process_watch_wait(process_watch_t *watch, uint64_t deadline_ms, unsigned long *error);
process_watch_result_t process_watch_kill(process_watch_t *watch, unsigned long *error);
/** @brief Release the pinned handle and consume *watch; NULL is an idempotent no-op. */
process_watch_result_t process_watch_close(process_watch_t **watch, unsigned long *error);
/** @brief Independent monotonic millisecond clock, not the process library clock.
 * A clock failure returns UINT64_MAX so pending waits fail closed at their deadline.
 */
uint64_t process_watch_now_ms(void);

/** @brief Return OK when condition is met, TIMEOUT while pending, ERROR on failure.
 * The callback supplies the native error on ERROR and must itself be bounded.
 */
typedef process_watch_result_t (*process_watch_predicate_t)(void *context, unsigned long *error);
/** @brief Evaluate a condition until true/error/deadline, equivalent to Python until.
 * Polling intervals only avoid busy-waiting; elapsed time never proves readiness.
 * Use process_watch_wait for process death: it waits directly on an OS event.
 */
process_watch_result_t process_watch_until(process_watch_predicate_t predicate,
                                           void *context,
                                           uint64_t deadline_ms,
                                           unsigned long *error);
