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
    bool joined_boundary, exceptional, release, observed, arguments, nested, method;
    unsigned parked, resumed, publications;
    uint64_t ids[2];
    gc_dispatch_test_t request;
} worker_case_t;

/* One test process per executable; all accesses during run hold its model lock. */
static worker_case_t state;

static bool collection_expected(void) {
    return state.publications == 2 || state.publications == 4;
}

static bool payload_ok(object_t *value) {
    object_array_t items = get_array_elements(value);
    return items.size == 1 && get_object_integer_value(items.items[0]).has_value
           && get_object_integer_value(items.items[0]).value == 7654321;
}

static operation_result_t
park_impl(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    REQUIRE(count == (state.arguments ? 2 : 0));
    REQUIRE(thread->data_stack->size == 0); /* static_call has popped every argument. */
    process_t *proc = thread->process;
    object_t *leaf = create_integer_object(proc, 7654321);
    object_t *value = create_array_object(proc, (object_array_t){&leaf, 1});
    DECREF(leaf);
    object_t *second_leaf = create_integer_object(proc, 7654321);
    object_t *second = create_array_object(proc, (object_array_t){&second_leaf, 1});
    DECREF(second_leaf);
    object_t *batch[] = {value, second, count ? args[0] : NULL, count ? args[1] : NULL, receiver};
    size_t batch_size = receiver ? 5 : count ? 4 : 2;
    REQUIRE((receiver != NULL) == state.method);
    gc_test_park_t registration = {0}, duplicate = {0};
    bool publish = state.parked < state.publications;
    if (publish) {
        size_t roots_before = proc->gc_roots_size;
        REQUIRE(gc_test_publish_park(thread, value, &registration));
        REQUIRE(gc_test_resume(thread, &registration));
        REQUIRE(proc->gc_roots_size == roots_before);
        object_t *invalid[] = {value, NULL};
        REQUIRE(!gc_test_publish_park_values(thread, (object_array_t){invalid, 2}, &registration));
        REQUIRE(proc->gc_roots_size == roots_before && !registration.thread);
        process_execution_enter(proc);
        REQUIRE(!gc_test_publish_park(thread, value, &registration));
        process_execution_leave(proc);
        REQUIRE(gc_test_publish_park_values(thread,
                                            (object_array_t){batch, batch_size},
                                            &registration));
        REQUIRE(!gc_test_publish_park(thread, value, &registration));
        REQUIRE(!gc_test_publish_park(thread, value, &duplicate));
        REQUIRE(!gc_test_resume(thread, &duplicate));
    }
    if (state.publications >= 3 && state.parked == 0) {
        REQUIRE(gc_test_resume(thread, &registration));
        REQUIRE(!gc_test_resume(thread, &registration));
        /* The old value can now die; a future publication must protect its replacement. */
        DECREF(value);
        leaf = create_integer_object(proc, 7654321);
        value = create_array_object(proc, (object_array_t){&leaf, 1});
        DECREF(leaf);
        batch[0] = value;
        publish = state.publications == 4;
        if (publish)
            REQUIRE(gc_test_publish_park_values(thread,
                                                (object_array_t){batch, batch_size},
                                                &registration));
    }
    /* Unregistered fixtures deliberately retain a C-local only value. */
    ++state.parked;
    broadcast_os_condition(proc->threads_changed);
    while (!state.release)
        REQUIRE(wait_os_condition(proc->threads_changed, proc->execution_mutex) == OS_THREAD_OK);
    /* Check refusal before touching the unpublished value. */
    REQUIRE(proc->gc_statistics->collections == (collection_expected() ? 1 : 0));
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1);
    if (publish) {
        process_execution_enter(proc);
        REQUIRE(!gc_test_resume(thread, &registration));
        process_execution_leave(proc);
        REQUIRE(gc_test_resume(thread, &registration));
        REQUIRE(!gc_test_resume(thread, &registration));
    }
    REQUIRE(payload_ok(value) && payload_ok(second));
    DECREF(second);
    if (count) {
        REQUIRE(args[0] != args[1] && payload_ok(args[0]) && payload_ok(args[1]));
        /* static_call will release both arguments after we return, including on exception. */
        DECREF(value);
        value = args[0];
        INCREF(value);
    }
    if (receiver) {
        REQUIRE(payload_ok(receiver));
        DECREF(value);
        value = receiver;
        INCREF(value);
    }
    ++state.resumed;
    return state.exceptional ? operation_exception(value) : operation_success(value);
}

static operation_result_t park(object_t **args, uint16_t count, thread_t *thread) {
    return park_impl(NULL, args, count, thread);
}

static operation_result_t
park_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return park_impl(receiver, args, count, thread);
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
    REQUIRE(state.resumed == 2 && proc->active_threads == 1 && proc->gc_roots_size == 0);
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1 && !thread->waiting_for);
}

/** @brief Explicitly publish the outer C frame before invoking the waiting builtin. */
static operation_result_t outer_park(object_t **args, uint16_t count, thread_t *thread) {
    REQUIRE(count == 2 && thread->data_stack->size == 0);
    object_t *leaf = create_integer_object(thread->process, 7654321);
    object_t *local = create_array_object(thread->process, (object_array_t){&leaf, 1});
    DECREF(leaf);
    object_t *values[] = {local, args[0], args[1]};
    object_t *frame = create_read_only_array_object(thread->process, (object_array_t){values, 3});
    gc_root_handle_t root = register_gc_root(thread->process, frame);
    REQUIRE(root.serial);
    DECREF(frame);
    static const builtin_function_t inner_descriptor = {.name = U"innerGcPark",
                                                        .execute = park,
                                                        .execute_method = park_method};
    static builtin_function_object_t inner_storage;
    for (size_t i = 0; i < count; ++i) {
        INCREF(args[i]);
        push_object_onto_stack(thread->data_stack, args[i]);
    }
    object_t *receiver = NULL;
    if (state.method) {
        object_t *receiver_leaf = create_integer_object(thread->process, 7654321);
        receiver = create_array_object(thread->process, (object_array_t){&receiver_leaf, 1});
        DECREF(receiver_leaf);
        /* Deliberately absent from the outer frame root: the method must publish it. */
    }
    instr_index_t saved = thread->instr_id;
    bool ok = call_object(get_builtin_function_object(&inner_storage, &inner_descriptor),
                          count,
                          receiver,
                          thread);
    thread->instr_id = saved;
    REQUIRE(ok == !state.exceptional);
    REQUIRE(payload_ok(local) && payload_ok(args[0]) && payload_ok(args[1]));
    object_t *result;
    if (ok) {
        REQUIRE(thread->data_stack->size == 1);
        result = pop_object_from_stack(thread->data_stack);
    } else {
        /* A direct builtin exception has no VM-generated trace at this boundary. */
        REQUIRE(thread->exception.value && !thread->exception.trace);
        result = thread->exception.value;
        thread->exception = (exception_t){0};
    }
    REQUIRE(payload_ok(result) && result == (receiver ? receiver : args[0]));
    REQUIRE(unregister_gc_root(thread->process, root));
    DECREF(local);
    DECREFIF(receiver);
    REQUIRE(payload_ok(result));
    return ok ? operation_success(result) : operation_exception(result);
}

static operation_result_t start(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    REQUIRE(count == 0);
    static const builtin_function_t descriptor = {.name = U"parkGcWorker", .execute = park};
    static builtin_function_object_t storage, outer_storage;
    static const builtin_function_t outer_descriptor = {.name = U"outerGcPark",
                                                        .execute = outer_park};
    object_t *dead = create_array_object(thread->process, (object_array_t){0});
    operation_result_t inserted = insert_array_elements(dead, 0, &dead, 1);
    REQUIRE(!inserted.is_exception);
    DECREF(inserted.value);
    DECREF(dead);
    for (size_t i = 0; i < 2; ++i) {
        object_t *arguments[2] = {NULL, NULL};
        if (state.arguments) {
            for (size_t j = 0; j < 2; ++j) {
                object_t *leaf = create_integer_object(thread->process, 7654321);
                arguments[j] = create_array_object(thread->process, (object_array_t){&leaf, 1});
                DECREF(leaf);
            }
        }
        operation_result_t result = spawn_program_thread(
            thread,
            state.nested ? get_builtin_function_object(&outer_storage, &outer_descriptor)
                         : get_builtin_function_object(&storage, &descriptor),
            state.arguments ? arguments : NULL,
            state.arguments ? 2 : 0);
        if (state.arguments) {
            DECREF(arguments[0]);
            DECREF(arguments[1]);
        }
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
    REQUIRE(state.request.attempted && state.request.collected == collection_expected());
    REQUIRE(proc->gc_statistics->collections == (collection_expected() ? 1 : 0));
    REQUIRE(os_mutex_depth(proc->execution_mutex) == 1);
    REQUIRE(payload_ok(peek_object_from_stack(thread->data_stack, 0)));
    if (collection_expected())
        REQUIRE(proc->gc_statistics->last.before.objects > proc->gc_statistics->last.after.objects);
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

bool test_gc_worker_admission(bool joined,
                              bool exceptional,
                              unsigned publications,
                              bool arguments,
                              bool nested,
                              bool method) {
    size_t baseline = get_allocated_memory_size();
    process_t *proc = create_process();
    gc_stats_t stats;
    REQUIRE(configure_gc_statistics(proc, &stats));
    /* Exclusive host ownership excludes external model clients, not the VM's own workers. */
    state = (worker_case_t){
        .joined_boundary = joined,
        .arguments = arguments,
        .nested = nested,
        .method = method,
        .exceptional = exceptional,
        .publications = publications,
        .request = {.exclusive_model_client = true, .published_workers = publications != 0}};
    bytecode_t *code = make_program(proc, true);
    REQUIRE(run_with_gc_dispatch_test(proc, code, &state.request) == 0);
    REQUIRE(state.observed && state.resumed == 2
            && state.request.collected == collection_expected());
    REQUIRE(stats.collections == (collection_expected() ? 2 : 1)
            && stats.explicit_collections == (collection_expected() ? 1 : 0)
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
