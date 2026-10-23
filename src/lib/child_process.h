/** @file child_process.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Owned child trees, monotonic deadlines and bounded byte streams (Linux/Windows).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHILD_OUTPUT_LIMIT 65536
#define CHILD_RESULT_ARGUMENT "@goat-result-channel@"
#define CHILD_SERVICE_ARGUMENT "@goat-service-channel@"
#define CHILD_PARENT_ARGUMENT "@goat-parent-watch@"

typedef struct child_process_t child_process_t;
/** @brief Absolute monotonic milliseconds; zero means unlimited. Share across probe phases. */
typedef uint64_t child_deadline_t;

typedef enum {
    CHILD_RUNNING,
    CHILD_EXITED,
    CHILD_SIGNALED,
    CHILD_TIMED_OUT,
    CHILD_STOPPED,
    CHILD_START_ERROR,
    CHILD_IO_ERROR
} child_status_t;

typedef struct {
    unsigned char data[CHILD_OUTPUT_LIMIT];
    size_t size;
    bool truncated;
} child_output_t;

typedef struct {
    child_status_t status;
    int64_t exit_code;
    int signal_number;
    unsigned long system_error;
    unsigned long cleanup_error;
    child_output_t out, err, result;
} child_result_t;

typedef struct {
    /** @brief UTF-8 executable, NULL launches this executable. No shell is involved. */
    const char *executable;
    /** @brief NULL-terminated arguments, including argv[0]. Strings are copied by start. */
    const char *const *arguments;
    child_deadline_t deadline;
    bool empty_stdin;
    bool capture_output;
    /** @brief Separate bounded result pipe; the exact reserved argument becomes its handle. */
    bool result_channel;
    /** @brief Pass a stable reference to this parent through the reserved argument. */
    bool parent_watch;
    /** @brief Explicit independent service; owner death does not stop it. Service must own its
     * work. */
    bool survive_parent;
    /** @brief Optional duplex endpoint; duplicated into child, caller retains its handle. */
    uintptr_t service_channel;
} child_options_t;

uint64_t child_monotonic_ms(void);
/** @brief Saturates on overflow; timeout zero means no deadline. */
child_deadline_t child_deadline_after(uint64_t timeout_ms);
/** @brief Returns an owned object even on launch failure; NULL only for invalid arguments. */
child_process_t *start_child_process(const child_options_t *options);
/** @brief Pumps all pipes, enforcing the original deadline; returned storage belongs to child. */
const child_result_t *wait_child_process(child_process_t *child);
/** @brief Nonblocking while running; final tree cleanup may wait. Same serialization rules. */
const child_result_t *poll_child_process(child_process_t *child);
/** @brief Release a survive_parent service without stopping it. No effect on ordinary children. */
void release_child_service(child_process_t *child);
/** @brief Stops the entire owned tree and waits for cleanup, preserving available output. */
const child_result_t *stop_child_process(child_process_t *child);
/** @brief Stops a running child before freeing it. No concurrent calls on one object. */
void destroy_child_process(child_process_t *child);
/** @brief Helpers for a child given a decimal native handle through a reserved argument. */
bool child_write_result(uintptr_t handle, const void *data, size_t size);
/** @brief Waits for the original parent, immune to PID reuse. Does not close the handle. */
bool child_wait_parent(uintptr_t handle, child_deadline_t deadline);
void child_close_channel(uintptr_t handle);
