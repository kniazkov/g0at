/** @file native_compiler.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Synchronous native compilation, independent of the VM and loader.
 */
#pragma once
#include "goat/unicode.h"
#include "lib/child_process.h"
#include "lib/cleanup_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NATIVE_COMPILE_OK,
    NATIVE_COMPILE_UNSUPPORTED,
    NATIVE_COMPILE_IO_ERROR,
    NATIVE_COMPILE_START_ERROR,
    NATIVE_COMPILE_FAILED,
    NATIVE_COMPILE_TIMED_OUT,
    NATIVE_COMPILE_CLEANUP_ERROR
} native_compile_status_t;

typedef struct {
    native_compile_status_t status;
    int system_error;
    int64_t exit_code;
    uint32_t windows_error;
    uint32_t windows_cleanup_error;
    int signal_number;
    int cleanup_error;
    char *diagnostics;
    size_t diagnostics_length;
    bool diagnostics_truncated;
    cleanup_status_t cleanup_status;
} native_compile_result_t;

/** @brief One refcounted lazy cleanup owner, shared by probes and every build in a run. */
typedef struct native_build_context_t native_build_context_t;
native_build_context_t *create_native_build_context(const char *cleanup_executable);
void retain_native_build_context(native_build_context_t *context);
/** @brief Final release asks the cleaner to finish; never manually removes its directory. */
void release_native_build_context(native_build_context_t *context);
cleanup_service_t *native_build_cleanup(native_build_context_t *context);

/** @brief Additional shell-free compiler arguments, before input/output arguments. */
typedef struct {
    const char *const *arguments;
    bool bundled_runtime;
    bool runtime_sources; /**< Rebuild embedded runtime with exactly the same flags. */
    child_deadline_t deadline;
} native_compile_options_t;

/** @brief Managed temporary compilation and atomic publication outside the cleanup tree.
 * The caller shares context with probes. NULL options uses the existing host runtime/flags.
 * A supplied options struct controls all target flags; no host architecture is silently added.
 */
native_compile_result_t compile_native_library_in_context(native_build_context_t *context,
                                                          const gchar_t *source,
                                                          const char *compiler,
                                                          const char *destination,
                                                          const native_compile_options_t *options);
/** @brief Managed child launch; the context must outlive the call. */
void native_compiler_run_in_context(native_build_context_t *context,
                                    native_compile_result_t *result,
                                    const char *const *arguments,
                                    const char *working_directory,
                                    child_deadline_t deadline);

/** @brief Builds beside destination, then atomically replaces it; compiler is one executable. */
native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination);
/** @brief Releases captured diagnostics. */
void destroy_native_compile_result(native_compile_result_t *result);

/** @brief Shared shell-free launch; zero deadline for user builds, one absolute deadline per probe.
 */
void native_compiler_run(native_compile_result_t *result,
                         const char *const *arguments,
                         child_deadline_t deadline);

/** @brief Private output location, retained until its loaded library is released. */
typedef struct {
    char *directory;
    char *library;
    native_build_context_t *context;
} native_workspace_t;

native_workspace_t *create_native_workspace(void);
native_workspace_t *create_native_workspace_in_context(native_build_context_t *context);
void destroy_native_workspace(native_workspace_t *workspace);
