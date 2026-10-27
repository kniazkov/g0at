/** @file c_output.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Default C filename, UTF-8 output and backend diagnostics.
 */
#include "c_output.h"

#include "codegen/c_module_output.h"
#include "codegen/native_compiler.h"
#include "codegen/native_runtime.h"
#include "codegen/native_toolchain.h"
#include "lib/files/file_system.h"
#include "lib/files/io.h"
#include "lib/memory/allocate.h"
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

static bool save_library(const options_t *options, const gchar_t *source) {
#ifdef _WIN32
    path_t *path = output_path(options->input_file, "dll");
#else
    path_t *path = output_path(options->input_file, "so");
#endif
    if (!path) {
        fprintf_utf8(stderr, get_messages()->native_input_conflict);
        FPRINTF(stderr, "\n");
        return false;
    }
    char *compiler_value = fs_environment("CC", NULL);
    const char *compiler = options->native_compiler ? options->native_compiler : compiler_value;
    native_build_context_t *context = options->native_build_context;
    if (context)
        retain_native_build_context(context);
    else
        context = create_native_build_context(NULL);
    native_toolchain_context_t *toolchains =
        create_native_toolchain_context(native_build_cleanup(context));
    native_toolchain_search_t search =
        find_native_toolchains(toolchains,
                               "host",
                               compiler && *compiler ? compiler : NULL,
                               options->native_probe_timeout,
                               false);
    if (!search.selected) {
        FPRINTF(stderr,
                "Native compiler unavailable: %s\n",
                search.diagnostic ? search.diagnostic : "no suitable compiler");
        destroy_native_toolchain_search(&search);
        destroy_native_toolchain_context(toolchains);
        release_native_build_context(context);
        FREE(compiler_value);
        free_path(path);
        return false;
    }
    compiler = search.selected->compiler;
    native_compile_options_t compile_options = {.arguments = search.selected->flags,
                                                .runtime_sources = true};
    /* Keep the final component unresolved: publication replaces a symlink, never its target. */
    native_compile_result_t result = compile_native_library_in_context(context,
                                                                       source,
                                                                       compiler,
                                                                       path->normal_path,
                                                                       &compile_options);
    if (result.diagnostics_length) {
        FWRITE(result.diagnostics, 1, result.diagnostics_length, stderr);
        if (result.diagnostics[result.diagnostics_length - 1] != '\n')
            FPUTC('\n', stderr);
    }
    if (result.diagnostics_truncated) {
        FPRINTF(stderr, "\n");
        fprintf_utf8(stderr, get_messages()->native_diagnostics_truncated);
        FPRINTF(stderr, "\n");
    }
    const gchar_t *reason = result.status == NATIVE_COMPILE_UNSUPPORTED   ? U"unsupported platform"
                            : result.status == NATIVE_COMPILE_START_ERROR ? U"cannot start compiler"
                            : result.status == NATIVE_COMPILE_FAILED      ? U"compiler failed"
                                                                     : U"file operation failed";
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
        FPRINTF(stderr, "\n");
    }
    destroy_native_compile_result(&result);
    destroy_native_toolchain_search(&search);
    destroy_native_toolchain_context(toolchains);
    release_native_build_context(context);
    FREE(compiler_value);
    free_path(path);
    return success;
}

bool output_c_module(const options_t *options, arena_t *arena, const node_t *root) {
    c_module_t *module = create_c_module(arena, root);
    c_module_output_t output = generate_c_module(arena, module);
    for (const c_module_failure_t *failure = output.failures; failure; failure = failure->next) {
        const gchar_t *reason =
            failure->dependency                           ? U"dependency unavailable"
            : failure->status == C_GENERATION_UNSUPPORTED ? U"unsupported lowering"
            : failure->status == C_GENERATION_NOT_PROVEN  ? U"missing proof or dependency"
                                                          : U"invalid generation request";
        fprintf_utf8(stderr, get_messages()->c_omitted, failure->entry->name.data, reason);
        if (failure->dependency)
            fprintf_utf8(stderr, U" (%s)", failure->dependency->name.data);
        else if (failure->failed_node)
            fprintf_utf8(stderr, U" (%s)", failure->failed_node->vtbl->type_name);
        FPRINTF(stderr, "\n");
    }
    bool success = true;
    if (options->save_c) {
        path_t *path = c_output_path(options->input_file);
        success = path && write_native_runtime_files(path->full_path)
                  && write_utf8_file(path->full_path, output.source.data);
        if (!success) {
            fprintf_utf8(stderr,
                         get_messages()->cannot_write_c_file,
                         path ? path->normal_path : options->input_file->normal_path);
            FPRINTF(stderr, "\n");
        }
        free_path(path);
    }
    if (success && options->save_library)
        success = save_library(options, output.source.data);
    if (success && options->print_c) {
        print_utf8(output.source.data);
        success = FFLUSH(stdout) == 0 && !FERROR(stdout);
    }
    FREE_STRING(output.source);
    return success;
}
