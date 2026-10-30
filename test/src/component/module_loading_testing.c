/** @file module_loading_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real snapshot, cleaner, native loader and VM acceptance fixture.
 */
/* Keep snapshot/search, stream and worker assertions active in Release (#369). */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/files/file_system.h"
#include "lib/files/io.h"
#include "lib/memory/allocate.h"
#include "lib/process/cleanup_service.h"
#include "lib/process/process_channel.h"
#include "model/thread.h"
#include "vm/module_loading.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

static size_t reports, writes;
static const char *fault;

static void marker(const char *name, const char *value) {
    char *root = fs_environment("GOAT_LOADING_MARKERS", NULL);
    if (!root)
        return;
    char *path = join_fs_path(root, name, NULL);
    FILE *file = FOPEN(path, "wb");
    assert(file);
    if (value)
        FPRINTF(file, "%s\n", value);
    else {
#ifdef _WIN32
        FPRINTF(file, "%lu\n", (unsigned long)_getpid());
#else
        FPRINTF(file, "%lu\n", (unsigned long)getpid());
#endif
    }
    assert(!FCLOSE(file));
    FREE(path);
    FREE(root);
}

static void report(void *state, size_t node, const char *message) {
    (void)state;
    ++reports;
    FPRINTF(stderr, "WARNING %zu %s\n", node, message);
}

static void *observe_entry_completion(void *state) {
    process_t *process = state;
    process_execution_enter(process);
    while (!process->main_thread->finished)
        assert(wait_os_condition(process->threads_changed, process->execution_mutex)
               == OS_THREAD_OK);
    FPRINTF(stdout, "ENTRY-FINISHED\n");
    FFLUSH(stdout);
    process_execution_leave(process);
    return NULL;
}

static bool write_bytes(void *state, FILE *file, const void *data, size_t size) {
    ++writes;
    if (writes == 1 && fault && !strcmp(fault, "late-worker")) {
        process_t *process = state;
        process_execution_enter(process);
        assert(process->main_thread->finished);
        process_execution_leave(process);
    }
    if (writes == 1 && fault
        && (!strcmp(fault, "exit-write") || !strcmp(fault, "exit-write-fail"))) {
        process_t *process = state;
        /* Deterministic host termination while the loader has suspended the VM lock. */
        process_execution_enter(process);
        process->exit_requested = true;
        process->exit_code = 23;
        process_execution_leave(process);
        if (!strcmp(fault, "exit-write-fail"))
            return false;
    }
    if (writes == 1 && fault && !strcmp(fault, "write-before"))
        return false;
    if (writes == 1 && fault && !strcmp(fault, "write-partial")) {
        assert(FWRITE(data, 1, size / 2, file) == size / 2);
        return false;
    }
    if (writes == 1 && fault && !strcmp(fault, "write-corrupt")) {
        const unsigned char *bytes = data;
        unsigned char first = bytes[0] ^ 1;
        return FWRITE(&first, 1, 1, file) == 1 && FWRITE(bytes + 1, 1, size - 1, file) == size - 1;
    }
    return FWRITE(data, 1, size, file) == size;
}

static int execute(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        marker("cleaner.pid", NULL);
        const char *failure = getenv("GOAT_LOADING_CLEANER_FAULT");
        if (failure && !strcmp(failure, "exit"))
            return 3;
        if (failure && !strcmp(failure, "silent")) {
            child_wait_parent((uintptr_t)strtoull(argv[2], NULL, 10), 0);
            return 0;
        }
        int status = 2;
        assert(cleanup_service_main(argc, argv, &status));
        marker("cleaner.done", "done");
        return status;
    }
    assert(argc == 4);
    /* A test-only PID marker, pinned only after a READY/HOLD handshake. */
    marker("owner.pid", NULL);
    fault = argv[3];
    module_search_t *search = NULL;
    module_search_error_t search_error = {0};
    module_snapshot_t *snapshot = NULL;
    module_graph_t *graph = NULL;
    assert(!prepare_module_search_from_environment(argv[1], NULL, &search, &search_error));
    module_graph_status_t graph_status =
        load_module_snapshot(argv[1], search, NULL, &snapshot, &graph);
    free_module_search(search);
    clear_module_search_error(&search_error);
    if (graph_status) {
        FPRINTF(stderr, "RESULT {\"graph\":%d}\n", graph_status);
        free_module_graph(graph);
        return 2;
    }
    module_loading_options_t options = {.mode = !strcmp(argv[2], "off")        ? NATIVE_OFF
                                                : !strcmp(argv[2], "required") ? NATIVE_REQUIRED
                                                                               : NATIVE_AUTO,
                                        .report = report,
                                        .write = write_bytes,
                                        .write_state = module_snapshot_process(snapshot)};
    module_loading_result_t prepared = prepare_module_snapshot_native(snapshot, &options);
    if (prepared.status) {
        FPRINTF(stderr, "PREPARE %s\n", prepared.diagnostic ? prepared.diagnostic : "");
        FPRINTF(stderr, "RESULT {\"prepare\":%d,\"node\":%zu}\n", prepared.status, prepared.node);
        clear_module_loading_result(&prepared);
        free_module_snapshot(snapshot);
        return 3;
    }
    clear_module_loading_result(&prepared);
    if (!strcmp(fault, "frozen")) {
        FPRINTF(stdout, "READY\n");
        fflush(stdout);
        assert(getchar() != EOF);
    }
    process_t *process = module_snapshot_process(snapshot);
    os_thread_t *observer = NULL;
    if (!strcmp(fault, "late-worker")) {
        assert(!process->main_thread->finished);
        assert(create_os_thread(&observer, observe_entry_completion, process, 0) == OS_THREAD_OK);
    }
    int status = run_module_file(process, module_files_main(module_snapshot_files(snapshot, 0)));
    if (observer) {
        assert(join_os_thread(observer, NULL) == OS_THREAD_OK);
        assert(destroy_os_thread(observer) == OS_THREAD_OK);
    }
    const char *directory = module_snapshot_native_directory(snapshot);
    if (directory)
        marker("directory", directory);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc), *nodes = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "status", status);
    yyjson_mut_obj_add_uint(doc, root, "reports", reports);
    yyjson_mut_obj_add_uint(doc, root, "writes", writes);
    size_t native_successes = 0;
    thread_t *thread = process->main_thread;
    do {
        native_successes += thread->native_successes;
        thread = thread->next;
    } while (thread != process->main_thread);
    yyjson_mut_obj_add_uint(doc, root, "native_successes", native_successes);
    yyjson_mut_obj_add_bool(doc, root, "terminal", process->native_failed);
    yyjson_mut_obj_add_str(doc, root, "directory", directory ? directory : "");
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);
    for (size_t i = 0; i < module_snapshot_count(snapshot); i++) {
        module_loading_statistics_t statistics = {0};
        bool present = module_snapshot_native_statistics(snapshot, i, &statistics);
        yyjson_mut_val *node = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, node, "present", present);
        yyjson_mut_obj_add_uint(doc, node, "extractions", statistics.extraction_attempts);
        yyjson_mut_obj_add_uint(doc, node, "loads", statistics.loader_attempts);
        yyjson_mut_obj_add_uint(doc, node, "warnings", statistics.warnings);
        yyjson_mut_obj_add_bool(doc, node, "loaded", statistics.loaded);
        yyjson_mut_obj_add_bool(doc, node, "unavailable", statistics.unavailable);
        yyjson_mut_arr_add_val(nodes, node);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    assert(json);
    FPRINTF(stderr, "RESULT %s\n", json);
    free(json);
    yyjson_mut_doc_free(doc);
    if (!strcmp(fault, "hold")) {
        FPRINTF(stdout, "HOLD\n");
        fflush(stdout);
        assert(getchar() != EOF);
        fs_info_t information;
        assert(!directory || inspect_fs_path(directory, true, &information, NULL));
    }
    free_module_snapshot(snapshot);
    return status;
}

int main(int argc, char **argv) {
    /* Pipe handshakes must not depend on whether another thread called input()
       before or after the main thread emitted its progress line. */
    SETBUF(stdout, NULL);
    fs_error_t error = {0};
    assert(fs_arguments(&argc, &argv, &error));
    int status = execute(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    assert(!get_allocated_memory_size());
    return status;
}
