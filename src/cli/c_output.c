/** @file c_output.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Default C filename, UTF-8 output and backend diagnostics.
 */
#include "c_output.h"

#include "codegen/c_module_output.h"
#include "codegen/native_compiler.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

path_t *output_path(const path_t *input, const char *extension) {
    const char *name =
        input && input->file_name && input->file_name[0] ? input->normal_path : "generated";
    const char *base = name;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    const char *dot = strrchr(base, '.');
    if (dot == base)
        dot = NULL;
    if (dot && !strcasecmp(dot + 1, extension))
        return NULL;
    size_t length = dot ? (size_t)(dot - name) : strlen(name);
    size_t suffix = strlen(extension);
    char *buffer = ALLOC(length + suffix + 2);
    memcpy(buffer, name, length);
    buffer[length] = '.';
    memcpy(buffer + length + 1, extension, suffix + 1);
    path_t *result = create_path(buffer);
    FREE(buffer);
    return result;
}

path_t *c_output_path(const path_t *input) {
    return output_path(input, "c");
}

static bool save_library(const options_t *options, const wchar_t *source) {
#ifdef _WIN32
    path_t *path = output_path(options->input_file, "dll");
#else
    path_t *path = output_path(options->input_file, "so");
#endif
    if (!path) {
        fprintf_utf8(stderr, get_messages()->native_input_conflict);
        fprintf(stderr, "\n");
        return false;
    }
    const char *compiler = getenv("CC");
    if (!compiler || !compiler[0])
        compiler =
#ifdef _WIN32
            "gcc";
#else
            "cc";
#endif
    /* Keep the final component unresolved: rename replaces a symlink, never its target. */
    native_compile_result_t result = compile_native_library(source, compiler, path->normal_path);
    if (result.diagnostics_length) {
        fwrite(result.diagnostics, 1, result.diagnostics_length, stderr);
        if (result.diagnostics[result.diagnostics_length - 1] != '\n')
            fputc('\n', stderr);
    }
    if (result.diagnostics_truncated) {
        fprintf(stderr, "\n");
        fprintf_utf8(stderr, get_messages()->native_diagnostics_truncated);
        fprintf(stderr, "\n");
    }
    const wchar_t *reason = result.status == NATIVE_COMPILE_UNSUPPORTED   ? L"unsupported platform"
                            : result.status == NATIVE_COMPILE_START_ERROR ? L"cannot start compiler"
                            : result.status == NATIVE_COMPILE_FAILED      ? L"compiler failed"
                                                                     : L"file operation failed";
    bool success = result.status == NATIVE_COMPILE_OK && !result.cleanup_error
                   && !result.windows_cleanup_error;
    if (!success) {
        fprintf_utf8(stderr,
                     get_messages()->native_compile_failed,
                     reason,
                     compiler,
                     path->normal_path,
                     result.exit_code,
                     result.signal_number,
                     result.system_error ? strerror(result.system_error) : "-",
                     result.cleanup_error ? strerror(result.cleanup_error) : "-",
                     result.windows_error,
                     result.windows_cleanup_error);
        fprintf(stderr, "\n");
    }
    destroy_native_compile_result(&result);
    free_path(path);
    return success;
}

bool output_c_module(const options_t *options, arena_t *arena, const node_t *root) {
    c_module_t *module = create_c_module(arena, root);
    c_module_output_t output = generate_c_module(arena, module);
    for (const c_module_failure_t *failure = output.failures; failure; failure = failure->next) {
        const wchar_t *reason =
            failure->dependency                           ? L"dependency unavailable"
            : failure->status == C_GENERATION_UNSUPPORTED ? L"unsupported lowering"
            : failure->status == C_GENERATION_NOT_PROVEN  ? L"missing proof or dependency"
                                                          : L"invalid generation request";
        fprintf_utf8(stderr, get_messages()->c_omitted, failure->entry->name.data, reason);
        if (failure->dependency)
            fprintf_utf8(stderr, L" (%s)", failure->dependency->name.data);
        else if (failure->failed_node)
            fprintf_utf8(stderr, L" (%s)", failure->failed_node->vtbl->type_name);
        fprintf(stderr, "\n");
    }
    bool success = true;
    if (options->save_c) {
        path_t *path = c_output_path(options->input_file);
        success = path && write_utf8_file(path->full_path, output.source.data);
        if (!success) {
            fprintf_utf8(stderr,
                         get_messages()->cannot_write_c_file,
                         path ? path->normal_path : options->input_file->normal_path);
            fprintf(stderr, "\n");
        }
        free_path(path);
    }
    if (success && options->save_library)
        success = save_library(options, output.source.data);
    if (success && options->print_c) {
        print_utf8(output.source.data);
        success = fflush(stdout) == 0 && !ferror(stdout);
    }
    FREE_STRING(output.source);
    return success;
}
