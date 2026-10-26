/** @file module_import_execution_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Literal imports through compilation, whole-graph preflight and execution.
 */
/* This acceptance harness uses assertions for checks and setup operations.
 * Keep them active when testing an optimized Release core as well. */
#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "common/module_build.h"
#include "common/module_compilation.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "model/thread.h"
#include "vm/gc.h"
#include "vm/module_graph_diagnostics.h"
#include "vm/module_snapshot.h"
#include "vm/module_writer.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <direct.h>
#    define chdir _chdir
#else
#    include <unistd.h>
#endif

static void report_graph(const module_graph_t *graph, module_graph_status_t status) {
    char *message = format_module_graph_diagnostics(graph);
    FPRINTF(stderr, "Invalid module graph: %d\n%s", status, message ? message : "");
    FREE(message);
}

static int compile(const char *source, const char *output, const module_build_options_t *options) {
    module_build_t *build = NULL;
    int result = 1;
    module_sources_t *sources = NULL;
    module_sources_error_t source_error = {0};
    module_imports_t *imports = NULL;
    module_imports_error_t import_error = {0};
    module_search_t *search = NULL;
    module_search_error_t search_error = {0};
    module_dependencies_t *dependencies = NULL;
    module_dependencies_error_t dependency_error = {0};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t compile_error = {0};
    module_writer_error_t write_error = {0};
    module_graph_t *graph = NULL, *source_graph = NULL;
    fs_error_t filesystem = {0};
    size_t length = strlen(source);
    bool descriptor = length >= 5 && !strcmp(source + length - 5, ".gmod");
    char *stage = malloc(strlen(output) + sizeof(".pending.gbin"));
    assert(stage);
    strcpy(stage, output);
    strcat(stage, ".pending.gbin");
    bool staged = false;
    if (module_sources_read(source, descriptor, NULL, &sources, &source_error)) {
        FPRINTF(stderr,
                "%s:%zu:%zu: %s\n",
                source_error.path ? source_error.path : source,
                source_error.row,
                source_error.column,
                source_error.message ? source_error.message : "source discovery failed");
    } else if (prepare_module_search_from_environment(source, NULL, &search, &search_error)) {
        FPRINTF(stderr, "Search preparation failed: %d\n", search_error.status);
    } else if (module_imports_collect(sources, 0, &imports, &import_error)) {
        FPRINTF(stderr,
                "file %zu:%zu:%zu: %s\n",
                import_error.site.file,
                import_error.site.row,
                import_error.site.column,
                import_error.message ? import_error.message : "import collection failed");
    } else if (bind_source_dependencies(imports, search, NULL, &dependencies, &dependency_error)) {
        const module_import_dependency_t *dependency =
            dependency_error.index < imports->count ? imports->dependencies + dependency_error.index
                                                    : NULL;
        char *message = format_module_search_error(&dependency_error.search,
                                                   dependency ? dependency->name : NULL,
                                                   dependency ? dependency->version : NULL,
                                                   source);
        FPRINTF(stderr, "Dependency binding failed: %s\n", message ? message : "unknown error");
        FREE(message);
    } else if (validate_source_graph(imports, search, NULL, &source_graph)) {
        report_graph(source_graph, MODULE_GRAPH_INVALID);
    } else if (options ? !build_module(sources,
                                       dependencies,
                                       source_graph,
                                       options,
                                       &build,
                                       &compile_error)
                       : !compile_module_sources_with_graph(sources,
                                                            dependencies,
                                                            source_graph,
                                                            &compilation,
                                                            &compile_error)) {
        FPRINTF(stderr,
                "file %zu:%zu:%zu: %s\n",
                compile_error.file,
                compile_error.row,
                compile_error.column,
                compile_error.message ? compile_error.message : "compilation failed");
    } else {
        if (build) {
            compilation = build->compilation;
            if (build->warning)
                FPRINTF(stderr, "warning: %s\n", build->warning);
        }
        for (size_t i = 0; i < compilation->warning_count; ++i) {
            const module_compilation_warning_t *warning = compilation->warnings + i;
            FPRINTF(stderr,
                    "file %zu:%zu:%zu: warning: %s\n",
                    warning->file,
                    warning->row,
                    warning->column,
                    warning->message);
        }
        module_writer_input_t input = {
            .metadata = compilation->metadata,
            .bytecode = {compilation->code->buffer, compilation->code->buffer_size}};
        module_writer_native_t native = {0};
        if (build)
            input = module_build_writer_input(build, &native);
        if (module_write_file(stage, &input, NULL, &write_error)) {
            FPRINTF(stderr, "Module packaging failed: %d\n", write_error.status);
        } else {
            staged = true;
            module_graph_status_t status = validate_module_graph(stage, search, NULL, &graph);
            if (status) {
                report_graph(graph, status);
            } else if (!prepare_fs_replace(stage, output, &filesystem)
                       || !publish_fs_replace(stage, output, &filesystem)) {
                FPRINTF(stderr, "Publication failed: %d\n", filesystem.status);
            } else {
                staged = false;
                result = 0;
            }
        }
    }
    if (staged)
        assert(remove_fs_entry(stage, NULL));
    free(stage);
    clear_fs_error(&filesystem);
    free_module_graph(graph);
    free_module_graph(source_graph);
    clear_module_writer_error(&write_error);
    if (build)
        free_module_build(build);
    else
        free_module_compilation(compilation);
    clear_module_compilation_error(&compile_error);
    free_module_dependencies(dependencies);
    clear_module_dependencies_error(&dependency_error);
    free_module_search(search);
    clear_module_search_error(&search_error);
    free_module_imports(imports);
    free_module_sources(sources);
    clear_module_sources_error(&source_error);
    return result;
}

static int execute(const char *archive, bool frozen, const char *directory) {
    int result = 1;
    module_search_t *search = NULL;
    module_search_error_t search_error = {0};
    module_snapshot_t *snapshot = NULL;
    module_graph_t *diagnostics = NULL;
    if (prepare_module_search_from_environment(archive, NULL, &search, &search_error)) {
        FPRINTF(stderr, "Search preparation failed: %d\n", search_error.status);
    } else {
        module_graph_status_t status =
            load_module_snapshot(archive, search, NULL, &snapshot, &diagnostics);
        if (status) {
            report_graph(diagnostics, status);
        } else {
            size_t count = module_snapshot_count(snapshot);
            unsigned char **images = calloc(count, sizeof(*images));
            assert(images);
            for (size_t i = 0; i < count; i++) {
                const bytecode_t *code =
                    validated_module_bytecode(module_snapshot_module(snapshot, i));
                images[i] = malloc(code->buffer_size);
                assert(images[i]);
                memcpy(images[i], code->buffer, code->buffer_size);
            }
            if (frozen) {
                FPRINTF(stdout, "READY\n");
                fflush(stdout);
                assert(getchar() != EOF);
                if (directory)
                    assert(chdir(directory) == 0);
            }
            process_t *process = module_snapshot_process(snapshot);
            module_files_t *files = module_snapshot_files(snapshot, 0);
            module_file_t *main_file = module_files_main(files);
            result = run_module_file(process, main_file);
            if (process->exit_requested) {
                /* Termination is not exception unwinding or successful file completion.
                 * Inspect only process state, then retain the image immutability checks. */
                assert(result == process->exit_code);
                assert(!process->main_thread->exception.value);
                assert(run_module_file(process, main_file) == result);
                assert(!process->main_thread->exception.value);
            } else {
                module_file_state_t state = module_file_state(main_file);
                object_t *scope = module_file_scope(main_file);
                /* These are borrowed from the persistent file, which remains a GC root.
                 * Do not add extra roots that could hide a runtime retention defect. */
                exception_t failure = module_file_exception(main_file);
                assert(state == (result ? MODULE_FILE_FAILED : MODULE_FILE_INITIALIZED));
                assert(process->main_thread->exception.value == failure.value);
                assert(process->main_thread->exception.trace == failure.trace);
                string_value_t original_trace = format_exception_trace(process, failure.trace);
                collect_garbage(process);
                assert(run_module_file(process, main_file) == result);
                assert(module_file_state(main_file) == state);
                assert(module_file_scope(main_file) == scope);
                exception_t repeated = module_file_exception(main_file);
                assert(repeated.value == failure.value && repeated.trace == failure.trace);
                assert(process->main_thread->exception.value == failure.value);
                assert(process->main_thread->exception.trace == failure.trace);
                string_value_t repeated_trace = format_exception_trace(process, repeated.trace);
                assert(original_trace.length == repeated_trace.length);
                assert(!original_trace.length
                       || !memcmp(original_trace.data,
                                  repeated_trace.data,
                                  original_trace.length * sizeof(*original_trace.data)));
                FREE_STRING(repeated_trace);
                if (result && process->main_thread->exception.value) {
                    function_access_t observer;
                    init_function_access(&observer, module_files_identity(files));
                    string_value_t message =
                        object_to_string_with_access(process->main_thread->exception.value,
                                                     &observer);
                    fprintf_utf8(stderr, U"Uncaught: %s\n", message.data);
                    FREE_STRING(message);
                    fprintf_utf8(stderr, U"%s", original_trace.data);
                    clear_function_access(&observer, true);
                }
                FREE_STRING(original_trace);
            }
            for (size_t i = 0; i < count; i++) {
                const bytecode_t *code =
                    validated_module_bytecode(module_snapshot_module(snapshot, i));
                assert(!memcmp(images[i], code->buffer, code->buffer_size));
                free(images[i]);
            }
            free(images);
        }
    }
    free_module_graph(diagnostics);
    free_module_snapshot(snapshot);
    free_module_search(search);
    clear_module_search_error(&search_error);
    return result;
}

int main(int argc, char **argv) {
    fs_error_t arguments_error = {0};
    assert(fs_arguments(&argc, &argv, &arguments_error));
    int result = 2;
    if (cleanup_service_main(argc, argv, &result)) {
        free_fs_arguments(argc, argv);
        return result;
    }
    if (argc == 4 && !strcmp(argv[1], "compile"))
        result = compile(argv[2], argv[3], NULL);
    else if (argc == 7 && !strcmp(argv[1], "native-build")) {
        module_build_options_t options = {.override_mode = strcmp(argv[4], "descriptor") != 0,
                                          .mode = !strcmp(argv[4], "off")        ? NATIVE_OFF
                                                  : !strcmp(argv[4], "required") ? NATIVE_REQUIRED
                                                                                 : NATIVE_AUTO,
                                          .target = strcmp(argv[5], "descriptor") ? argv[5] : NULL,
                                          .compiler = strcmp(argv[6], "auto") ? argv[6] : NULL};
        result = compile(argv[2], argv[3], &options);
    } else if (argc == 3 && (!strcmp(argv[1], "binary") || !strcmp(argv[1], "frozen")))
        result = execute(argv[2], !strcmp(argv[1], "frozen"), NULL);
    else if (argc == 4 && !strcmp(argv[1], "frozen-chdir"))
        result = execute(argv[2], true, argv[3]);
    else
        FPRINTF(stderr,
                "Usage: compile SOURCE OUTPUT | binary ARCHIVE | frozen ARCHIVE | frozen-chdir "
                "ARCHIVE DIR\n");
    free_fs_arguments(argc, argv);
    clear_fs_error(&arguments_error);
    assert(!get_allocated_memory_size());
    return result;
}
