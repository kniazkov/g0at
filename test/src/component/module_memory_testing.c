/** @file module_memory_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Subprocess allocation failures across complete snapshot preparation.
 */
#include "allocation_fault.h"
#include "lib/memory/allocate.h"
#include "model/thread.h"
#include "vm/module_snapshot.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t parse_size(const char *text) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    assert(end && !*end && value <= SIZE_MAX);
    return (size_t)value;
}

static void checked(const module_validation_diagnostic_t *event, void *context) {
    /* This callback must not allocate, even with a persistent injected failure. */
    assert(!event->archive || !strstr(event->archive, "fallback"));
    if (event->check == MODULE_CHECK_INPUT && event->state == MODULE_CHECK_PASSED)
        (*(size_t *)context)++;
}

static void execute(module_snapshot_t *snapshot) {
    process_t *process = module_snapshot_process(snapshot);
    module_files_t *files = module_snapshot_files(snapshot, 0);
    module_file_t *file = module_files_main(files);
    thread_t *thread = process->main_thread;
    context_t *caller = thread->context;
    context_t *context = create_module_file_context(file, caller);
    assert(context);
    thread->context = context;
    thread->instr_id = module_file_entry(file);
    assert(!run(process, module_files_bytecode(files)));
    assert(thread->context == context);
    assert(destroy_context(context) == caller);
    thread->context = caller;
    puts("EXECUTED");
}

int main(int argc, char **argv) {
    if (argc == 2) {
        if (!strcmp(argv[1], "add-overflow"))
            allocation_size_add(SIZE_MAX, 1);
        else if (!strcmp(argv[1], "multiply-overflow"))
            allocation_size_multiply(SIZE_MAX / 2 + 1, 2);
        else if (!strcmp(argv[1], "boundaries")) {
            assert(allocation_size_add(SIZE_MAX - 1, 1) == SIZE_MAX);
            assert(allocation_size_multiply(SIZE_MAX, 1) == SIZE_MAX);
            assert(!allocation_size_multiply(SIZE_MAX, 0));
            printf("%zu\n", sizeof(size_t) * 8);
            return 0;
        }
        return 99; /* Overflow must have exited before this point. */
    }
    assert(argc == 3 || argc == 4 || argc == 5);
    size_t fail = parse_size(argv[2]), checks = 0;
    module_search_t *search = NULL;
    module_search_error_t error = {0};
    assert(prepare_module_search(argv[1], "fallback", NULL, &search, &error) == MODULE_SEARCH_OK);
    size_t prior = get_allocated_memory_size();
    module_validation_options_t options = {SIZE_MAX, SIZE_MAX, checked, &checks};
    if (argc == 5) {
        options.max_archive = parse_size(argv[3]);
        options.max_expanded = parse_size(argv[4]);
    }
    module_snapshot_t *snapshot = NULL;
    module_graph_t *diagnostics = NULL;
    allocation_fault_set_sticky(argc != 4);
    allocation_fault_start(fail);
    module_graph_status_t status =
        load_module_snapshot(argv[1], search, &options, &snapshot, &diagnostics);
    bool triggered = allocation_fault_triggered();
    size_t allocations = allocation_fault_stop();
    if (triggered) {
        assert(status == MODULE_GRAPH_MEMORY);
        assert(!snapshot && !diagnostics);
        assert(prior == get_allocated_memory_size());
    } else if (status) {
        assert(!snapshot);
        assert(status == MODULE_GRAPH_INVALID);
        free_module_graph(diagnostics);
        assert(prior == get_allocated_memory_size());
    } else {
        assert(snapshot && !diagnostics);
        execute(snapshot);
        free_module_snapshot(snapshot);
        assert(prior == get_allocated_memory_size());
    }
    free_module_search(search);
    clear_module_search_error(&error);
    assert(!get_allocated_memory_size());
    printf("{\"status\":%d,\"allocations\":%zu,\"triggered\":%s,\"checks\":%zu}\n",
           status,
           allocations,
           triggered ? "true" : "false",
           checks);
    return 0;
}
