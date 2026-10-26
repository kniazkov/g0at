/** @file os_thread.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Portable C threads and synchronization, independent of VM threads and processes.
 */
#pragma once
#include <stddef.h>

typedef struct os_thread_t os_thread_t;
typedef struct os_mutex_t os_mutex_t;
typedef struct os_condition_t os_condition_t;
typedef void *(*os_thread_function_t)(void *argument);

typedef enum {
    OS_THREAD_OK,
    OS_THREAD_INVALID,
    OS_THREAD_BUSY,
    OS_THREAD_DEADLOCK,
    OS_THREAD_ERROR
} os_thread_status_t;

/** @brief Starts immediately; zero stack size uses the OS default, otherwise reserves bytes.
 * Sets *out to NULL on failure. The callback must return normally; no cancellation/detach API.
 */
os_thread_status_t create_os_thread(os_thread_t **out,
                                    os_thread_function_t function,
                                    void *argument,
                                    size_t stack_size);
/** @brief Joins once and optionally returns the callback result. Self-join reports DEADLOCK.
 * Calls on the same thread handle must be serialized by the caller.
 */
os_thread_status_t join_os_thread(os_thread_t *thread, void **result);
/** @brief Frees a joined thread; an unjoined thread returns BUSY. NULL is a no-op. */
os_thread_status_t destroy_os_thread(os_thread_t *thread);

os_thread_status_t create_os_mutex(os_mutex_t **out);
/** @brief Creates a reentrant mutex: each successful lock/try-lock requires an unlock. */
os_thread_status_t create_os_recursive_mutex(os_mutex_t **out);
/** @brief Locks; owner reentry on a nonrecursive mutex reports DEADLOCK. */
os_thread_status_t lock_os_mutex(os_mutex_t *mutex);
os_thread_status_t try_lock_os_mutex(os_mutex_t *mutex);
/** @brief Only the owning thread may unlock; misuse reports INVALID. */
os_thread_status_t unlock_os_mutex(os_mutex_t *mutex);
/** @brief Current caller's acquisition depth, or zero for NULL/unowned/foreign-owned mutex. */
size_t os_mutex_depth(const os_mutex_t *mutex);
/** @brief Requires no concurrent users; locked mutexes/active condition waits return BUSY. */
os_thread_status_t destroy_os_mutex(os_mutex_t *mutex);

os_thread_status_t create_os_condition(os_condition_t **out);
/** @brief Atomically releases an owned mutex and waits, returning with it locked again.
 * Always test the predicate in a loop: wakeups may be spurious. All concurrent waits on one
 * condition must use the same mutex. Signal/broadcast do not store notifications.
 * A recursive mutex is fully released during the wait and its original depth is restored.
 */
os_thread_status_t wait_os_condition(os_condition_t *condition, os_mutex_t *mutex);
os_thread_status_t signal_os_condition(os_condition_t *condition);
os_thread_status_t broadcast_os_condition(os_condition_t *condition);
/** @brief Requires no concurrent users; active waiters return BUSY. NULL is a no-op. */
os_thread_status_t destroy_os_condition(os_condition_t *condition);
