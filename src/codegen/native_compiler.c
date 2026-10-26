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
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

struct native_build_context_t {
    atomic_size_t references;
    cleanup_service_t *cleanup;
};

native_build_context_t *create_native_build_context(const char *executable) {
    native_build_context_t *context = ALLOC(sizeof(*context));
    atomic_init(&context->references, 1);
    context->cleanup = create_cleanup_service(executable);
    if (!context->cleanup) {
        FREE(context);
        return NULL;
    }
    return context;
}

void retain_native_build_context(native_build_context_t *context) {
    if (context)
        atomic_fetch_add_explicit(&context->references, 1, memory_order_relaxed);
}

void release_native_build_context(native_build_context_t *context) {
    if (!context || atomic_fetch_sub_explicit(&context->references, 1, memory_order_acq_rel) != 1)
        return;
    /* Only the cleaner removes files, after all loaded libraries and compiler trees stop.
       Failure after READY does not alter the caller's execution result. */
    finish_cleanup_service(context->cleanup);
    destroy_cleanup_service(context->cleanup);
    FREE(context);
}

cleanup_service_t *native_build_cleanup(native_build_context_t *context) {
    return context ? context->cleanup : NULL;
}

static void collect_result(native_compile_result_t *result, const child_result_t *run) {
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
}

void native_compiler_run_in_context(native_build_context_t *context,
                                    native_compile_result_t *result,
                                    const char *const *arguments,
                                    const char *working_directory,
                                    child_deadline_t deadline) {
    child_options_t options = {.executable = arguments[0],
                               .arguments = arguments,
                               .working_directory = working_directory,
                               .deadline = deadline};
    child_result_t *run = CALLOC(sizeof(*run));
    result->cleanup_status =
        context ? run_cleanup_process(context->cleanup, &options, run) : CLEANUP_INVALID;
    if (result->cleanup_status == CLEANUP_OK)
        collect_result(result, run);
    else {
        result->status = NATIVE_COMPILE_CLEANUP_ERROR;
        result->exit_code = -1;
    }
    FREE(run);
}

void native_compiler_run(native_compile_result_t *result,
                         const char *const *arguments,
                         child_deadline_t deadline) {
    native_build_context_t *context = create_native_build_context(NULL);
    native_compiler_run_in_context(context, result, arguments, NULL, deadline);
    release_native_build_context(context);
}

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

static const char *const default_arguments[] = {"-std=c11",
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
                                                "-march=x86-64",
#        else
                                                "-m32",
                                                "-march=i686",
#        endif
                                                "-Wl,--no-undefined",
                                                "-Wl,--exclude-all-symbols",
#    else
#        if defined(__x86_64__)
                                                "-march=x86-64",
#        elif defined(__aarch64__)
                                                "-march=armv8-a",
#        endif
                                                "-Wl,-z,defs",
#    endif
                                                NULL};

/** @brief A saved output belongs to the user, never to the cleaner's tree. */
static bool publish_output(const char *output, const char *target, fs_error_t *error) {
    fs_info_t info;
    if (!inspect_fs_path(output, false, &info, error) || info.kind != FS_FILE || !info.size)
        return false;
    void *bytes = NULL;
    size_t size = 0;
    if (!read_fs_regular_file(output, 512u * 1024u * 1024u, &bytes, &size, error) || !size) {
        FREE(bytes);
        return false;
    }
    FILE *file = NULL;
    char *staged = create_fs_temporary(target, false, &file, error);
    bool written = staged && write_fs_file(file, bytes, size, staged, error);
    if (file && !close_fs_file(file, staged, error))
        written = false;
    FREE(bytes);
    bool published = written && prepare_fs_replace(staged, target, error)
                     && publish_fs_replace(staged, target, error);
    if (staged && !published)
        remove_fs_entry(staged, NULL);
    FREE(staged);
    return published;
}

native_compile_result_t compile_native_library_in_context(native_build_context_t *context,
                                                          const gchar_t *source,
                                                          const char *compiler,
                                                          const char *destination,
                                                          const native_compile_options_t *options) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!context || !source || !compiler || !*compiler || !destination || !*destination) {
        result.system_error = EINVAL;
        return result;
    }
    result.cleanup_status = ensure_cleanup_service(context->cleanup);
    if (result.cleanup_status != CLEANUP_OK) {
        result.status = NATIVE_COMPILE_CLEANUP_ERROR;
        return result;
    }
    fs_error_t error = {0};
    char *target = absolute_fs_path(destination, &error);
    char *prefix = join_fs_path(cleanup_service_directory(context->cleanup), "build", &error);
    char *directory = target && prefix ? create_fs_temporary(prefix, true, NULL, &error) : NULL;
    FREE(prefix);
    if (!directory) {
        file_error(&result, &error, false);
        clear_fs_error(&error);
        FREE(target);
        return result;
    }
    char *input = join_fs_path(directory, "module.c", &error);
    /* PE export directory includes this basename; keep the requested artifact's name. */
    const char *name = target;
    for (const char *p = target; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    char *output = join_fs_path(directory, name, &error);
    /* A leading '-' must stay a filename even through GCC's linker driver. */
    char *relative_output = join_fs_path(".", name, &error);
    char *runtime = join_fs_path(directory, GOAT_NATIVE_ARCHIVE_NAME, &error);
    bool source_runtime = !options || options->runtime_sources;
    bool bundled = !source_runtime && options && options->bundled_runtime;
    if (!input || !output || !relative_output || !runtime
        || (bundled && !write_native_runtime_files(input))
        || (source_runtime && !write_native_runtime_sources(input))
        || !write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto done;
    }
    const char *const *flags = options ? options->arguments : default_arguments;
    size_t count = 0;
    if (flags)
        while (flags[count])
            count++;
    const char *runtime_names[] = {"runtime_strings.c",
                                   "runtime_str.c",
                                   "runtime_number_format.c",
                                   "runtime_unicode.c"};
    const char **arguments = ALLOC((count + 12) * sizeof(*arguments));
    size_t index = 0;
    arguments[index++] = compiler;
    for (size_t i = 0; i < count; i++)
        arguments[index++] = flags[i];
#    ifdef _WIN32
    /* GCC otherwise puts assembler output in inherited TEMP, whose Unicode
       path older MinGW tools cannot open. Keep every intermediate in the
       managed working directory without mutating the owner's environment. */
    arguments[index++] = "-save-temps=obj";
#    endif
    arguments[index++] = "-o";
    /* The process API sets this Unicode directory through the native OS API.
       Keep compiler-visible input paths relative ASCII names: external Windows
       toolchains need not decode our UTF-8 absolute directory names in argv. */
    arguments[index++] = relative_output;
    arguments[index++] = "./module.c";
    if (bundled)
        arguments[index++] = GOAT_NATIVE_ARCHIVE_NAME;
    if (source_runtime)
        for (size_t i = 0; i < 4; i++)
            arguments[index++] = runtime_names[i];
    arguments[index++] = "-lm";
    arguments[index] = NULL;
    native_compiler_run_in_context(context,
                                   &result,
                                   arguments,
                                   directory,
                                   options ? options->deadline : 0);
    FREE(arguments);
    if (result.status == NATIVE_COMPILE_OK) {
        if (!publish_output(output, target, &error)) {
            result.status = NATIVE_COMPILE_IO_ERROR;
            file_error(&result, &error, false);
        }
    }
done:
    /* Deliberately no manual deletion. Even transport/tree failure cannot authorize
       deleting files while a compiler might still be writing them. */
    clear_fs_error(&error);
    FREE(runtime);
    FREE(input);
    FREE(output);
    FREE(relative_output);
    FREE(directory);
    FREE(target);
    return result;
}

native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    if (!source || !compiler || !*compiler || !destination || !*destination)
        return (native_compile_result_t){.status = NATIVE_COMPILE_IO_ERROR,
                                         .system_error = EINVAL,
                                         .exit_code = -1};
    native_build_context_t *context = create_native_build_context(NULL);
    native_compile_result_t result =
        compile_native_library_in_context(context, source, compiler, destination, NULL);
    release_native_build_context(context);
    return result;
}

native_workspace_t *create_native_workspace_in_context(native_build_context_t *context) {
    if (!context || ensure_cleanup_service(context->cleanup) != CLEANUP_OK)
        return NULL;
    char *prefix = join_fs_path(cleanup_service_directory(context->cleanup), "native", NULL);
    char *directory = prefix ? create_fs_temporary(prefix, true, NULL, NULL) : NULL;
    FREE(prefix);
    if (!directory)
        return NULL;
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
#    ifdef _WIN32
    workspace->library = join_fs_path(directory, "module.dll", NULL);
#    else
    workspace->library = join_fs_path(directory, "module.so", NULL);
#    endif
    workspace->context = context;
    retain_native_build_context(context);
    return workspace;
}

native_workspace_t *create_native_workspace(void) {
    native_build_context_t *context = create_native_build_context(NULL);
    native_workspace_t *workspace = create_native_workspace_in_context(context);
    release_native_build_context(context);
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    release_native_build_context(workspace->context);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#else
native_compile_result_t compile_native_library_in_context(native_build_context_t *context,
                                                          const gchar_t *source,
                                                          const char *compiler,
                                                          const char *destination,
                                                          const native_compile_options_t *options) {
    (void)context;
    (void)source;
    (void)compiler;
    (void)destination;
    (void)options;
    return (native_compile_result_t){.status = NATIVE_COMPILE_UNSUPPORTED, .exit_code = -1};
}

native_compile_result_t
compile_native_library(const gchar_t *source, const char *compiler, const char *destination) {
    return compile_native_library_in_context(NULL, source, compiler, destination, NULL);
}

native_workspace_t *create_native_workspace_in_context(native_build_context_t *context) {
    (void)context;
    return NULL;
}

native_workspace_t *create_native_workspace(void) {
    return NULL;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    (void)workspace;
}
#endif
