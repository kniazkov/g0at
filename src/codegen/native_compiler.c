/** @file native_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared compiler diagnostics and result lifetime.
 */
#include "native_compiler.h"

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/io.h"
#include "native_runtime.h"

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

#if defined(__linux__) || defined(_WIN32)
static void file_error(native_compile_result_t *result, const fs_error_t *error, bool cleanup) {
#    ifdef _WIN32
    if (cleanup)
        result->windows_cleanup_error = error->native_code;
    else
        result->windows_error = error->native_code;
#    else
    if (cleanup)
        result->cleanup_error = error->native_code;
    else
        result->system_error = error->native_code;
#    endif
}

native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!source || !compiler || !*compiler || !destination || !*destination) {
        result.system_error = EINVAL;
        return result;
    }
    fs_error_t error = {0};
    char *target = absolute_fs_path(destination, &error);
    char *directory = target ? create_fs_temporary(target, true, NULL, &error) : NULL;
    if (!directory) {
        file_error(&result, &error, false);
        clear_fs_error(&error);
        FREE(target);
        return result;
    }
    char *input = join_fs_path(directory, "module.c", &error);
    const char *name = "module.so";
#    ifdef _WIN32
    /* PE export directory includes this basename. */
    name = target;
    for (const char *p = target; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
#    endif
    char *output = join_fs_path(directory, name, &error);
    char *runtime = join_fs_path(directory, GOAT_NATIVE_ARCHIVE_NAME, &error);
    if (!write_native_runtime_files(input) || !write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto cleanup;
    }
    const char *args[] = {compiler,
                          "-std=c11",
                          "-O2",
#    ifndef _WIN32
                          "-fPIC",
#    endif
                          "-shared",
                          "-fno-fast-math",
                          "-ffp-contract=off",
#    ifdef _WIN32
                          "-static",
                          "-static-libgcc",
                          "-DGOAT_NATIVE_BUILD",
#        ifdef _WIN64
                          "-m64",
#        else
                          "-m32",
#        endif
                          "-Wl,--no-undefined",
                          "-Wl,--exclude-all-symbols",
#    else
                          "-Wl,-z,defs",
#    endif
                          "-o",
                          output,
                          input,
                          runtime,
                          "-lm",
                          NULL};
    native_compiler_run(&result, args, 0);
    if (result.cleanup_error || result.windows_cleanup_error)
        goto release;
    if (result.status != NATIVE_COMPILE_OK)
        goto cleanup;
    result.status = NATIVE_COMPILE_IO_ERROR;
    fs_info_t info;
    if (!inspect_fs_path(output, false, &info, &error)) {
        file_error(&result, &error, false);
        goto cleanup;
    }
    if (info.kind != FS_FILE || !info.size) {
        result.system_error = EIO;
        goto cleanup;
    }
    if (!prepare_fs_replace(output, target, &error)
        || !publish_fs_replace(output, target, &error)) {
        file_error(&result, &error, false);
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_OK;
cleanup:
    if (!remove_fs_tree(directory, &error))
        file_error(&result, &error, true);
release:
    clear_fs_error(&error);
    FREE(runtime);
    FREE(input);
    FREE(output);
    FREE(directory);
    FREE(target);
    return result;
}

native_workspace_t *create_native_workspace(void) {
    char *base = temporary_fs_directory(NULL);
    char *prefix = base ? join_fs_path(base, "goat-native", NULL) : NULL;
    char *directory = prefix ? create_fs_temporary(prefix, true, NULL, NULL) : NULL;
    FREE(prefix);
    FREE(base);
    if (!directory)
        return NULL;
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
#    ifdef _WIN32
    workspace->library = join_fs_path(directory, "module.dll", NULL);
#    else
    workspace->library = join_fs_path(directory, "module.so", NULL);
#    endif
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    remove_fs_tree(workspace->directory, NULL);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#endif
