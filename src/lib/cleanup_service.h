/** @file cleanup_service.h
 * @copyright 2026 Ivan Kniazkov
 * @brief One lazy independent cleaner per run; all temporary process launches go through it.
 */
#pragma once
#include "child_process.h"
#include "file_system.h"

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
 * executable/working_directory/arguments/deadline are copied; other child_options fields must be
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
