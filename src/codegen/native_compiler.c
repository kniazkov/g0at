/** @file native_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared compiler diagnostics and result lifetime.
 */
#include "native_compiler.h"

#include "lib/allocate.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void native_compiler_run(native_compile_result_t *result,
                         const char *const *arguments,
                         child_deadline_t deadline) {
    child_options_t options = {.executable = arguments[0],
                               .arguments = arguments,
                               .deadline = deadline,
                               .empty_stdin = true,
                               .capture_output = true};
    child_process_t *child = start_child_process(&options);
    const child_result_t *run = wait_child_process(child);
    result->exit_code = run->exit_code;
    result->signal_number = run->signal_number;
#ifdef _WIN32
    result->windows_error = run->system_error;
    result->windows_cleanup_error = run->cleanup_error;
#else
    result->system_error = (int)run->system_error;
    result->cleanup_error = (int)run->cleanup_error;
#endif
    result->status = run->status == CHILD_START_ERROR                 ? NATIVE_COMPILE_START_ERROR
                     : run->status == CHILD_EXITED && !run->exit_code ? NATIVE_COMPILE_OK
                     : run->status == CHILD_TIMED_OUT                 ? NATIVE_COMPILE_TIMED_OUT
                                                                      : NATIVE_COMPILE_FAILED;
    if ((run->system_error && run->status != CHILD_START_ERROR) || run->cleanup_error)
        result->status = NATIVE_COMPILE_IO_ERROR;
    size_t err = run->err.size, out = run->out.size;
    if (out > CHILD_OUTPUT_LIMIT - err)
        out = CHILD_OUTPUT_LIMIT - err;
    result->diagnostics_length = err + out;
    result->diagnostics_truncated =
        run->err.truncated || run->out.truncated || out != run->out.size;
    result->diagnostics = ALLOC(err + out + 1);
    memcpy(result->diagnostics, run->err.data, err);
    memcpy(result->diagnostics + err, run->out.data, out);
    result->diagnostics[err + out] = 0;
    destroy_child_process(child);
}

#if !defined(__linux__) && !defined(_WIN32)
native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    (void)source;
    (void)compiler;
    (void)destination;
    return (native_compile_result_t){.status = NATIVE_COMPILE_UNSUPPORTED, .exit_code = -1};
}

native_workspace_t *create_native_workspace(void) {
    return NULL;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    (void)workspace;
}
#endif

void destroy_native_compile_result(native_compile_result_t *result) {
    FREE(result->diagnostics);
    result->diagnostics = NULL;
    result->diagnostics_length = 0;
}
