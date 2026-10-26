/** @file module_program.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional module CLI; no source or dependency execution during builds.
 */
#include "module_program.h"

#include "c_output.h"
#include "common/module_build.h"
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/io.h"
#include "model/exception.h"
#include "model/object.h"
#include "model/thread.h"
#include "module/module_native.h"
#include "native_execution.h"
#include "resources/messages.h"
#include "vm/module_graph_diagnostics.h"
#include "vm/module_loading.h"
#include "vm/vm.h"

#include <stdlib.h>
#include <string.h>

#ifndef GOAT_BUILD_ID
#    define GOAT_BUILD_ID "source-v1"
#endif

static bool extension(const options_t *options, const char *value) {
    return options->input_file->extension && !strcasecmp(options->input_file->extension, value);
}

static void graph_error(const module_graph_t *graph, module_graph_status_t status) {
    char *text = format_module_graph_diagnostics(graph);
    FPRINTF(stderr, "Module graph validation failed (%d).\n%s", status, text ? text : "");
    FREE(text);
}

static bool producer(module_compilation_t *compilation) {
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(compilation->metadata, NULL);
    if (!doc)
        return false;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc), *array = yyjson_mut_arr(doc);
    yyjson_mut_val *record = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, record, "name", "Goat");
    yyjson_mut_obj_add_str(doc, record, "version", "0.0.0");
    yyjson_mut_obj_add_str(doc, record, "buildId", GOAT_BUILD_ID);
    yyjson_mut_arr_append(array, record);
    yyjson_mut_obj_put(root, yyjson_mut_str(doc, "producers"), array);
    size_t size = 0;
    char *json = yyjson_mut_write(doc, 0, &size);
    yyjson_doc *metadata = NULL;
    if (json)
        goat_module_json_read(json, size, GOAT_MODULE_METADATA, &metadata);
    free(json);
    yyjson_mut_doc_free(doc);
    if (!metadata)
        return false;
    yyjson_doc_free(compilation->metadata);
    compilation->metadata = metadata;
    return true;
}

/** Iterative parent creation through the shared UTF-8 filesystem adapter. */
static bool directories(const char *path) {
    path_t *parts = create_path(path);
    vector_t *missing = create_vector();
    char *cursor = absolute_fs_path(parts->full_path, NULL);
    bool ok = cursor != NULL;
    while (ok) {
        fs_info_t info;
        fs_error_t error = {0};
        if (inspect_fs_path(cursor, true, &info, &error)) {
            ok = info.kind == FS_DIRECTORY;
            clear_fs_error(&error);
            break;
        }
        ok = error.status == FS_NOT_FOUND;
        clear_fs_error(&error);
        if (!ok)
            break;
        append_to_vector(missing, cursor);
        path_t *parent = create_path(cursor);
        cursor = parent->dir_name ? absolute_fs_path(parent->dir_name, NULL) : NULL;
        free_path(parent);
        ok = cursor && strcmp(cursor, (char *)missing->data[missing->size - 1]);
    }
    FREE(cursor);
    for (size_t i = missing->size; ok && i; --i)
        ok = create_fs_directory(missing->data[i - 1], NULL);
    for (size_t i = 0; i < missing->size; ++i)
        FREE(missing->data[i]);
    destroy_vector(missing);
    free_path(parts);
    return ok;
}

static char *destination(const options_t *options, const module_sources_t *sources) {
    if (!sources->descriptor) {
        path_t *path = output_path(options->input_file, "gbin");
        char *result = path ? absolute_fs_path(path->full_path, NULL) : NULL;
        free_path(path);
        return result;
    }
    char *root = options->module_directory
                     ? absolute_fs_path(options->module_directory->full_path, NULL)
                     : join_fs_path(sources->base_directory, "modules", NULL);
    yyjson_val *metadata = yyjson_doc_get_root(sources->descriptor);
    const char *name = yyjson_get_str(yyjson_obj_get(metadata, "name"));
    const char *version = yyjson_get_str(yyjson_obj_get(metadata, "version"));
    char *directory = root ? join_fs_path(root, name, NULL) : NULL;
    FREE(root);
    char *result = NULL;
    if (directory && directories(directory)) {
        size_t size = strlen(version) + sizeof(".gbin");
        char *filename = ALLOC(size);
        snprintf(filename, size, "%s.gbin", version);
        result = join_fs_path(directory, filename, NULL);
        FREE(filename);
    }
    FREE(directory);
    return result;
}

/** Reject report/publication aliases without forbidding ordinary archive replacement. */
static bool aliases(const char *left, const char *right) {
    if (!left || !right)
        return false;
    path_t *a = create_path(left), *b = create_path(right);
    bool same = paths_refer_to_same_file(a, b);
    free_path(a);
    free_path(b);
    return same;
}

static bool
safe_output(const char *output, const module_sources_t *sources, const module_graph_t *graph) {
    if (!output)
        return true;
    if (sources) {
        if (aliases(output, sources->descriptor_path))
            return false;
        for (size_t i = 0; i < sources->count; ++i)
            if (aliases(output, sources->files[i].path))
                return false;
    }
    for (size_t i = 0; i < module_graph_node_count(graph); ++i)
        if (aliases(output, module_graph_node(graph, i)->archive))
            return false;
    return true;
}

static native_execution_mode_t mode(const options_t *options, const module_sources_t *sources) {
    if (options->native_explicit || !sources || !sources->descriptor)
        return sources && !sources->descriptor ? options->native_execution
               : options->native_explicit      ? options->native_execution
                                               : NATIVE_AUTO;
    yyjson_val *root = yyjson_doc_get_root(sources->descriptor);
    const char *value = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "native"), "mode"));
    return value && !strcmp(value, "off")        ? NATIVE_OFF
           : value && !strcmp(value, "required") ? NATIVE_REQUIRED
                                                 : NATIVE_AUTO;
}

static void loading_warning(void *unused, size_t node, const char *message) {
    (void)unused;
    (void)node;
    FPRINTF(stderr, "warning: %s\n", message);
}

static int execute_snapshot(const options_t *options,
                            module_snapshot_t *snapshot,
                            native_execution_mode_t native_mode,
                            native_build_context_t *context,
                            bool native_disabled) {
    module_loading_options_t loading = {.mode = native_disabled ? NATIVE_OFF : native_mode,
                                        .cleanup = native_build_cleanup(context),
                                        .report = loading_warning};
    module_loading_result_t preparation = prepare_module_snapshot_native(snapshot, &loading);
    if (preparation.status) {
        FPRINTF(stderr,
                "%s\n",
                preparation.diagnostic ? preparation.diagnostic
                                       : "Module native preparation failed.");
        clear_module_loading_result(&preparation);
        return 1;
    }
    clear_module_loading_result(&preparation);
    module_files_t *files = module_snapshot_files(snapshot, 0);
    if (options->print_bytecode) {
        string_value_t text = bytecode_to_text(module_files_bytecode(files));
        print_utf8(text.data);
        FREE_STRING(text);
    }
    process_t *process = module_snapshot_process(snapshot);
    int status = run_module_file(process, module_files_main(files));
    thread_t *thread = process->main_thread;
    do {
        if (thread->native_diagnostic)
            FPRINTF(stderr, "%s\n", thread->native_diagnostic);
        else if (thread->native_status)
            FPRINTF(stderr, "Native backend failed (status %u).\n", thread->native_status);
        if (thread->exception.value && !thread->failure_observed && !process->native_failed) {
            function_access_t observer;
            init_function_access(&observer, module_files_identity(files));
            string_value_t value = object_to_string_with_access(thread->exception.value, &observer);
            clear_function_access(&observer, true);
            fprintf_utf8(stderr, get_messages()->uncaught_exception, value.data);
            FPRINTF(stderr, "\n");
            FREE_STRING(value);
        }
        if (thread->exception.trace && !thread->failure_observed) {
            string_value_t trace = format_exception_trace(process, thread->exception.trace);
            if (trace.data)
                fprintf_utf8(stderr, U"%s", trace.data);
            FREE_STRING(trace);
        }
        thread = thread->next;
    } while (thread != process->main_thread);
    size_t bound = 0;
    for (size_t i = 0; i < module_snapshot_count(snapshot); ++i) {
        module_loading_statistics_t statistics = {0};
        if (module_snapshot_native_statistics(snapshot, i, &statistics))
            bound += statistics.bound_functions;
    }
    native_execution_report_t report = {.preparation = native_mode == NATIVE_OFF ? "disabled"
                                                       : native_disabled         ? "unavailable"
                                                                                 : "ready",
                                        .bound_functions = bound};
    options_t effective = *options;
    effective.native_execution = native_mode;
    if (!output_native_report(&effective, &report, process) && !process->exit_requested)
        status = 1;
    return status;
}

int try_module_program(const options_t *options, bool *handled) {
    *handled = false;
    bool descriptor = extension(options, "gmod");
    bool binary = options->run_binary || extension(options, "gbin");
    if (binary) {
        FILE *file = FOPEN(options->launch_path, "rb");
        char magic[8] = {0};
        size_t count = file ? FREAD(magic, 1, sizeof(magic), file) : 0;
        if (file)
            FCLOSE(file);
        /* The legacy reader owns raw GOATBC01 bytecode, GOATPG01 programs and
         * their malformed/truncated diagnostics. Only ZIP inputs use modules. */
        if (count < 2 || memcmp(magic, "PK", 2))
            return 0;
    }
    *handled = descriptor || binary;
    int result = 1;
    bool native_disabled = false;
    module_sources_t *sources = NULL;
    module_sources_error_t source_error = {0};
    module_imports_t *imports = NULL;
    module_imports_error_t import_error = {0};
    module_search_t *search = NULL;
    module_search_error_t search_error = {0};
    module_dependencies_t *dependencies = NULL;
    module_dependencies_error_t dependency_error = {0};
    module_graph_t *source_graph = NULL, *graph = NULL;
    module_build_t *build = NULL;
    module_compilation_error_t compile_error = {0};
    module_writer_error_t writer_error = {0};
    module_snapshot_t *snapshot = NULL;
    native_build_context_t *context = NULL;
    void *archive = NULL;
    size_t archive_size = 0;
    char *output = NULL;
    if (!binary) {
        if (module_sources_read(options->launch_path, descriptor, NULL, &sources, &source_error)) {
            if (!descriptor && source_error.status == MODULE_SOURCES_SYNTAX)
                goto done; /* Legacy parser owns ordinary-source syntax diagnostics. */
            *handled = true;
            FPRINTF(stderr,
                    "%s:%zu:%zu: %s (%s)\n",
                    source_error.path ? source_error.path : options->launch_path,
                    source_error.row,
                    source_error.column,
                    source_error.message ? source_error.message : "Invalid module sources",
                    goat_module_json_error_string(source_error.json));
            goto done;
        }
        if (module_imports_collect(sources, 0, &imports, &import_error)) {
            *handled = true;
            FPRINTF(stderr,
                    "file %zu:%zu:%zu: %s\n",
                    import_error.site.file,
                    import_error.site.row,
                    import_error.site.column,
                    import_error.message ? import_error.message : "Invalid imports");
            goto done;
        }
        if (!descriptor && sources->count == 1 && !sources->files[0].includes.count
            && !imports->count)
            goto done;
        *handled = true;
    }
    if (options->print_c || options->save_c || options->save_library || options->print_source_code
        || options->print_analysis || options->analysis_output_file || options->graph_output_file) {
        FPRINTF(stderr, "Source/C/analysis/graph export is not supported for module CLI input.\n");
        goto done;
    }
    if (prepare_module_search_from_environment(
            options->launch_path,
            options->module_directory ? options->module_directory->full_path : NULL,
            &search,
            &search_error)) {
        char *message = format_module_search_error(&search_error, NULL, NULL, options->launch_path);
        FPRINTF(stderr, "%s\n", message ? message : "Module search preparation failed.");
        FREE(message);
        goto done;
    }
    native_execution_mode_t native_mode = mode(options, sources);
    context = options->native_build_context;
    if (context)
        retain_native_build_context(context);
    else
        context = create_native_build_context(NULL);
    if (!context)
        goto done;
    if (binary) {
        module_graph_status_t status =
            load_module_snapshot(options->launch_path, search, NULL, &snapshot, &graph);
        if (status) {
            graph_error(graph, status);
            goto done;
        }
    } else {
        for (size_t i = 0; i < imports->unused_count; ++i)
            FPRINTF(stderr,
                    "warning: unused dependency %s@%s\n",
                    imports->unused_defaults[i].name,
                    imports->unused_defaults[i].version);
        module_graph_status_t status = validate_source_graph(imports, search, NULL, &source_graph);
        if (status) {
            graph_error(source_graph, status);
            goto done;
        }
        if (bind_source_dependencies(imports, search, NULL, &dependencies, &dependency_error)) {
            const module_import_dependency_t *dependency =
                dependency_error.index < imports->count
                    ? imports->dependencies + dependency_error.index
                    : NULL;
            char *message = format_module_search_error(&dependency_error.search,
                                                       dependency ? dependency->name : NULL,
                                                       dependency ? dependency->version : NULL,
                                                       options->launch_path);
            FPRINTF(stderr, "%s\n", message ? message : "Dependency binding failed.");
            FREE(message);
            goto done;
        }
        module_build_options_t build_options = {.override_mode = true,
                                                .mode = native_mode,
                                                .target = options->native_target,
                                                .compiler = options->native_compiler,
                                                .probe_timeout_seconds =
                                                    options->native_probe_timeout,
                                                .context = context};
        if (!build_module(sources,
                          dependencies,
                          source_graph,
                          &build_options,
                          &build,
                          &compile_error)) {
            FPRINTF(stderr,
                    "file %zu:%zu:%zu: %s\n",
                    compile_error.file,
                    compile_error.row,
                    compile_error.column,
                    compile_error.message ? compile_error.message : "Module compilation failed.");
            goto done;
        }
        if (build->warning) {
            FPRINTF(stderr, "warning: %s\n", build->warning);
            cleanup_status_t startup;
            if (cleanup_service_startup_result(native_build_cleanup(context), &startup)
                && startup != CLEANUP_OK)
                native_disabled = true; /* One startup diagnostic for build and execution. */
        }
        for (size_t i = 0; i < build->compilation->warning_count; ++i) {
            const module_compilation_warning_t *warning = build->compilation->warnings + i;
            if (options->enable_warnings)
                FPRINTF(stderr,
                        "file %zu:%zu:%zu: warning: %s\n",
                        warning->file,
                        warning->row,
                        warning->column,
                        warning->message);
        }
        if (!producer(build->compilation))
            goto done;
        module_writer_native_t native = {0};
        module_writer_input_t input = module_build_writer_input(build, &native);
        if (module_write_memory(&input, NULL, &archive, &archive_size, &writer_error)) {
            FPRINTF(stderr, "Module packaging failed (%d).\n", writer_error.status);
            goto done;
        }
        status = load_module_snapshot_memory(archive,
                                             archive_size,
                                             options->launch_path,
                                             search,
                                             NULL,
                                             &snapshot,
                                             &graph);
        if (status) {
            graph_error(graph, status);
            goto done;
        }
        if (options->compile_only) {
            if (options->native_output_file
                && !safe_output(options->native_output_file->full_path, sources, source_graph)) {
                FPRINTF(stderr, "Native report conflicts with a source or module archive.\n");
                goto done;
            }
            output = destination(options, sources);
            if (output
                && (!safe_output(output, sources, source_graph)
                    || (options->native_output_file
                        && aliases(output, options->native_output_file->full_path)))) {
                FPRINTF(stderr, "Module output conflicts with a source, dependency or report.\n");
                goto done;
            }
            if (!output || module_write_file(output, &input, NULL, &writer_error)) {
                FPRINTF(stderr,
                        "Module publication failed (%d): %s\n",
                        writer_error.status,
                        output ? output : "could not create destination directory");
                goto done;
            }
            if (options->print_bytecode) {
                string_value_t text = bytecode_to_text(build->compilation->code);
                print_utf8(text.data);
                FREE_STRING(text);
            }
            options_t effective = *options;
            effective.native_execution = native_mode;
            yyjson_val *metadata = yyjson_doc_get_root(build->compilation->metadata);
            yyjson_val *variants = yyjson_obj_get(metadata, "native");
            size_t bound = 0;
            for (size_t i = 0; i < yyjson_arr_size(variants); ++i)
                bound += goat_module_native_function_count(yyjson_arr_get(variants, i));
            native_execution_report_t report = {.preparation = native_mode == NATIVE_OFF
                                                                   ? "disabled"
                                                               : bound ? "ready"
                                                                       : "empty",
                                                .bound_functions = bound};
            result = output_native_report(&effective, &report, NULL) ? 0 : 1;
            goto done;
        }
    }
    if (options->native_output_file
        && !safe_output(options->native_output_file->full_path,
                        sources,
                        module_snapshot_graph(snapshot))) {
        FPRINTF(stderr, "Native report conflicts with a source or module archive.\n");
        goto done;
    }
    result = execute_snapshot(options, snapshot, native_mode, context, native_disabled);
done:
    FREE(output);
    free_module_snapshot(snapshot);
    free_module_writer_bytes(archive);
    free_module_graph(graph);
    free_module_graph(source_graph);
    clear_module_writer_error(&writer_error);
    free_module_build(build);
    clear_module_compilation_error(&compile_error);
    free_module_dependencies(dependencies);
    clear_module_dependencies_error(&dependency_error);
    free_module_search(search);
    clear_module_search_error(&search_error);
    free_module_imports(imports);
    free_module_sources(sources);
    clear_module_sources_error(&source_error);
    release_native_build_context(context);
    return result;
}
