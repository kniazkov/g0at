/** @file gc_worker_admission_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic real-worker refusal at the test dispatcher boundary.
 */
#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/thread.h"
#include "vm/gc_admission_testing.h"
#include "vm/gc_stats.h"

#include <stdio.h>
#include <stdlib.h>

/* Fail immediately even in Release. Returning from a broken barrier would hang the peers. */
#define REQUIRE(expr)                                                                              \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "Worker GC admission: %s:%d: %s\n", __FILE__, __LINE__, #expr);        \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    bool joined_boundary, exceptional, release, observed;
    unsigned parked, resumed;
    uint64_t ids[2];
    gc_dispatch_test_t request;
} worker_case_t;

/* One test process per executable; all accesses during run hold its model lock. */
static worker_case_t state;

static bool payload_ok(object_t *value) {
    object_array_t items = get_array_elements(value);
    return items.size == 1 && get_object_integer_value(items.items[0]).has_value
           && get_object_integer_value(items.items[0]).value == 7654321;
}

static operation_result_t park(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    REQUIRE(count == 0);
    process_t *proc = thread->process;
    object_t *leaf = create_integer_object(proc, 7654321);
    object_t *value = create_array_object(proc, (object_array_t){&leaf, 1});
    DECREF(leaf);
    /* Deliberately C-local only: waiting is not evidence of tracing-safe publication. */
    ++state.parked;
    broadcast_os_condition(proc->threads_changed);
    while (!state.release)
        REQUIRE(wait_os_condition(proc->threads_changed, proc->execution_mutex) == OS_THREAD_OK);
    /* Check refusal before touching the unpublished value. */
    REQUIRE(proc->gc_statistics->collections == 0);
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1 && payload_ok(value));
    ++state.resumed;
    return state.exceptional ? operation_exception(value) : operation_success(value);
}

static void release_and_join(thread_t *thread) {
    process_t *proc = thread->process;
    REQUIRE(state.parked == 2 && state.resumed == 0 && proc->active_threads == 3);
    state.release = true;
    broadcast_os_condition(proc->threads_changed);
    for (size_t i = 0; i < 2; ++i) {
        operation_result_t result = join_program_thread(thread, state.ids[i]);
        REQUIRE(result.is_exception == state.exceptional && payload_ok(result.value));
        DECREF(result.value);
    }
    REQUIRE(state.resumed == 2 && proc->active_threads == 1);
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1 && !thread->waiting_for);
}

static operation_result_t start(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    REQUIRE(count == 0);
    static const builtin_function_t descriptor = {.name = U"parkGcWorker", .execute = park};
    static builtin_function_object_t storage;
    for (size_t i = 0; i < 2; ++i) {
        operation_result_t result =
            spawn_program_thread(thread,
                                 get_builtin_function_object(&storage, &descriptor),
                                 NULL,
                                 0);
        REQUIRE(!result.is_exception);
        state.ids[i] = (uint64_t)get_object_integer_value(result.value).value;
        DECREF(result.value);
    }
    process_t *proc = thread->process;
    while (state.parked != 2)
        REQUIRE(wait_os_condition(proc->threads_changed, proc->execution_mutex) == OS_THREAD_OK);
    REQUIRE(!state.request.attempted && proc->gc_statistics->collections == 0);
    if (state.joined_boundary)
        release_and_join(thread);
    return operation_success(get_null_object());
}

static operation_result_t inspect(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    REQUIRE(count == 0);
    process_t *proc = thread->process;
    REQUIRE(state.request.attempted && !state.request.collected);
    REQUIRE(proc->gc_statistics->collections == 0);
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1);
    REQUIRE(payload_ok(peek_object_from_stack(thread->data_stack, 0)));
    if (state.joined_boundary) {
        /* Only the historical first_worker guard now excludes this boundary. */
        REQUIRE(proc->active_threads == 1 && !proc->finished_workers);
        unsigned workers = 0;
        for (thread_t *item = thread->next; item != thread; item = item->next) {
            REQUIRE(item->finished && !item->worker && !item->waiting_for);
            ++workers;
        }
        REQUIRE(workers == 2);
    } else {
        REQUIRE(state.parked == 2 && !state.release && state.resumed == 0);
        release_and_join(thread);
    }
    state.observed = true;
    return operation_success(get_boolean_object(true));
}

static uint32_t install(process_t *proc,
                        data_builder_t *data,
                        const builtin_function_t *descriptor,
                        builtin_function_object_t *storage,
                        object_static_string_t *key_storage) {
    object_t *key = get_static_string_object(key_storage, descriptor->name);
    create_object_property(proc->main_thread->context->data,
                           key,
                           get_builtin_function_object(storage, descriptor),
                           true);
    return add_string_to_data_segment(data, descriptor->name);
}

static bytecode_t *make_program(process_t *proc, bool workers) {
    static const builtin_function_t start_descriptor = {.name = U"startGcWorkers",
                                                        .execute = start};
    static const builtin_function_t inspect_descriptor = {.name = U"inspectGcWorkers",
                                                          .execute = inspect};
    static builtin_function_object_t start_storage, inspect_storage;
    static object_static_string_t start_key, inspect_key;
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    if (workers) {
        uint32_t name = install(proc, data, &start_descriptor, &start_storage, &start_key);
        add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = name});
        add_instruction(builder, (instruction_t){.opcode = CALL});
        add_instruction(builder, (instruction_t){.opcode = POP});
    }
    add_instruction(builder, (instruction_t){.opcode = ILOAD32, .arg1 = 7654321});
    add_instruction(builder, (instruction_t){.opcode = ARRAY, .arg1 = 1});
    if (workers) {
        uint32_t name = install(proc, data, &inspect_descriptor, &inspect_storage, &inspect_key);
        add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = name});
        add_instruction(builder, (instruction_t){.opcode = CALL});
    }
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

bool test_gc_worker_admission(bool joined, bool exceptional) {
    size_t baseline = get_allocated_memory_size();
    process_t *proc = create_process();
    gc_stats_t stats;
    REQUIRE(configure_gc_statistics(proc, &stats));
    /* Exclusive host ownership excludes external model clients, not the VM's own workers. */
    state = (worker_case_t){.joined_boundary = joined,
                            .exceptional = exceptional,
                            .request = {.exclusive_model_client = true}};
    bytecode_t *code = make_program(proc, true);
    REQUIRE(run_with_gc_dispatch_test(proc, code, &state.request) == 0);
    REQUIRE(state.observed && state.resumed == 2 && !state.request.collected);
    REQUIRE(stats.collections == 1 && stats.explicit_collections == 0
            && stats.run_end_collections == 1);
    REQUIRE(!proc->active_threads && !proc->finished_workers && !proc->execution);
    REQUIRE(!os_mutex_depth(proc->execution_mutex));
    REQUIRE(peek_object_from_stack(proc->main_thread->data_stack, 0) == get_boolean_object(true));
    free_bytecode(code);
    while (proc->main_thread->data_stack->size) {
        object_t *value = pop_object_from_stack(proc->main_thread->data_stack);
        DECREF(value);
    }
    /* The run-scoped worker history must not ban a fresh, single-participant run. */
    code = make_program(proc, false);
    proc->main_thread->instr_id = 0;
    REQUIRE(configure_gc_statistics(proc, &stats));
    REQUIRE(run_with_gc_dispatch_test(proc, code, &state.request) == 0);
    REQUIRE(state.request.attempted && state.request.collected);
    REQUIRE(stats.collections == 2 && stats.explicit_collections == 1
            && stats.run_end_collections == 1);
    REQUIRE(payload_ok(peek_object_from_stack(proc->main_thread->data_stack, 0)));
    REQUIRE(!os_mutex_depth(proc->execution_mutex));
    destroy_process(proc);
    free_bytecode(code);
    REQUIRE(get_allocated_memory_size() == baseline);
    return true;
}
