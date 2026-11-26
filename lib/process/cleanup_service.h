/** @file cleanup_service.h
 * @copyright 2026 Ivan Kniazkov
 * @brief One lazy independent cleaner per run; all temporary process launches go through it.
 *
 * Client destruction and owner-process death are different events. The cleaner may
 * still own live children or files after a timeout or a destroyed client context.
 * Consumers must release loaded resources before requesting final cleanup, and
 * report cleanup separately from the child command result.
 */
#pragma once
#include "lib/files/file_system.h"
#include "lib/process/child_process.h"

#define CLEANUP_SERVICE_ARGUMENT "--goat-cleanup-service"
#define CLEANUP_START_MS 1000
#define CLEANUP_WAIT_MS 5000
#define CLEANUP_RETRY_MS 30000

typedef struct cleanup_service_t cleanup_service_t;

typedef enum {
    CLEANUP_OK,
    CLEANUP_START_FAILED,
    CLEANUP_TIMEOUT,
    CLEANUP_CONNECTION_LOST,
    CLEANUP_DELETE_FAILED,
    CLEANUP_TREE_FAILED,
    CLEANUP_CLOSED,
    CLEANUP_INVALID
} cleanup_status_t;

typedef struct {
    cleanup_status_t status;
    fs_status_t file_status;
    unsigned long native_code;
} cleanup_result_t;

/** @brief Last startup step reached; failure does not imply a known child exit cause. */
typedef enum {
    CLEANUP_START_CHANNEL,
    CLEANUP_START_PROCESS,
    CLEANUP_START_HEADER,
    CLEANUP_START_BODY,
    CLEANUP_START_VALIDATE,
    CLEANUP_START_ACK,
    CLEANUP_START_READY
} cleanup_start_stage_t;

/** @brief Immutable evidence from the one startup attempt, without polling/stopping the child. */
typedef struct {
    cleanup_status_t status;
    cleanup_start_stage_t stage;
    /** @brief process_channel.h status, or -1 when no transport operation was attempted. */
    int transport;
    uint64_t elapsed_ms;
    unsigned reply_type;
    size_t reply_size;
} cleanup_start_diagnostic_t;

/** @brief Copy a completed attempt under the service lock; false before ensure or on invalid input.
 * Does not start, poll, wait for or stop a process. No concurrent destroy is allowed.
 */
bool cleanup_service_startup_diagnostic(cleanup_service_t *service,
                                        cleanup_start_diagnostic_t *diagnostic);

/** @brief Allocation only. Create one context per owner run, share it between native consumers.
 * executable is the host implementing cleanup_service_main, NULL means this executable.
 * Copies executable; no service process or temporary directory yet. NULL on sync init failure.
 */
cleanup_service_t *create_cleanup_service(const char *executable);
/** @brief Concurrent first calls share one attempt and one 1 s monotonic deadline.
 * A failed attempt is permanent. Readiness remains confirmed even if the cleaner later dies.
 */
cleanup_status_t ensure_cleanup_service(cleanup_service_t *service);
/** @brief Borrowed absolute UTF-8 directory, available after READY until destroy.
 * Call after ensure, while no concurrent destroy. Only cleaner owns removal of this directory.
 */
const char *cleanup_service_directory(const cleanup_service_t *service);
/** @brief Synchronous managed launch, not post-launch PID registration.
 * Cleaner owns the child/tree before any child code can run. All output is captured, stdin empty.
 * executable/working_directory/arguments/deadline/result_channel are copied; other fields must be
 * zero. A child failure is reported in result with CLEANUP_OK; transport failure is distinct.
 * Serializes calls. After connection loss no new launches; existing owner code may continue.
 */
cleanup_status_t run_cleanup_process(cleanup_service_t *service,
                                     const child_options_t *options,
                                     child_result_t *result);
/** @brief Request final cleanup, waiting at most 5 s including lock acquisition and sending.
 * Must only be called after users have released/unloaded resources. Closes this context to work.
 * Timeout/lost connection does not cancel cleaner; it may still own processes/files.
 * Repeated calls return the recorded result without restarting cleanup or its deadline.
 */
cleanup_result_t finish_cleanup_service(cleanup_service_t *service);
/** @brief No concurrent users. Frees client state/channels, never stops cleaner or removes files.
 * Cleaner still waits for actual owner death; this call does not mean the owner has exited.
 */
void destroy_cleanup_service(cleanup_service_t *service);
/** @brief Host entry point, before normal CLI parsing. True when reserved mode was handled.
 * Host returns *exit_code immediately. Protocol arguments are internal, not a user command.
 */
bool cleanup_service_main(int argc, char **argv, int *exit_code);

/** @brief Observe a completed startup attempt without starting a service. */
bool cleanup_service_startup_result(cleanup_service_t *service, cleanup_status_t *status);

/** @brief Write bounded startup evidence (no paths/output/environment), or an explicit unavailable
 * marker. Same locking/lifetime rules as startup_diagnostic. Buffer must have nonzero capacity.
 */
void cleanup_service_format_startup(cleanup_service_t *service, char *buffer, size_t capacity);
