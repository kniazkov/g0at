/** @file gc_dispatch_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A real dispatcher collection, not a direct model-only GC call.
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
#include <string.h>

bool test_gc_worker_admission(bool joined, bool exceptional);

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "GC dispatch check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

/* This test host runs one process at a time and creates no competing model clients. */
static uint64_t expected_passes;
static size_t expected_depth;

static bool array_payload(object_t *value) {
    for (size_t depth = 0; depth < 3 && value->vtbl->type == TYPE_ARRAY; ++depth) {
        object_array_t elements = get_array_elements(value);
        if (elements.size != 1)
            return false;
        value = elements.items[0];
    }
    int_value_t integer = get_object_integer_value(value);
    return integer.has_value && integer.value == 7654321;
}

/** @brief Runs after the candidate and before final GC, with the result owned only by the stack. */
static operation_result_t observe(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    gc_stats_t *stats = thread->process->gc_statistics;
    bool ok = count == 0 && thread->data_stack->size == 1
              && array_payload(peek_object_from_stack(thread->data_stack, 0))
              && stats->collections == expected_passes
              && stats->explicit_collections == expected_passes && !stats->run_end_collections
              && os_mutex_depth(thread->process->execution_mutex) == expected_depth;
    if (expected_passes)
        ok = ok && stats->last.before.objects > stats->last.after.objects;
    return operation_success(get_boolean_object(ok));
}

static bytecode_t *program(process_t *proc, bool nested, bool no_array, bool throwing) {
    static const builtin_function_t descriptor = {.name = U"observeGcDispatch", .execute = observe};
    static builtin_function_object_t function_storage;
    static object_static_string_t key_storage, nested_storage;
    object_t *key = get_static_string_object(&key_storage, descriptor.name);
    object_t *function = get_builtin_function_object(&function_storage, &descriptor);
    if (!get_object_property(proc->main_thread->context->data, key))
        create_object_property(proc->main_thread->context->data, key, function, true);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t observer = add_string_to_data_segment(data, descriptor.name);
    if (no_array) {
        add_instruction(builder, (instruction_t){.opcode = END});
    } else if (nested) {
        object_t *name = get_static_string_object(&nested_storage, U"nestedGcDispatch");
        object_t *callee =
            create_function_object(proc, NULL, 0, 5, proc->main_thread->context->data, NULL, true);
        create_object_property(proc->main_thread->context->data, name, callee, true);
        DECREF(callee);
        uint32_t nested_name = add_string_to_data_segment(data, U"nestedGcDispatch");
        add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = nested_name});
        add_instruction(builder, (instruction_t){.opcode = CALL});
        add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = observer});
        add_instruction(builder, (instruction_t){.opcode = CALL});
        add_instruction(builder, (instruction_t){.opcode = END});
        add_instruction(builder, (instruction_t){.opcode = ILOAD32, .arg1 = 7654321});
        add_instruction(builder, (instruction_t){.opcode = ARRAY, .arg1 = 1});
        add_instruction(builder, (instruction_t){.opcode = RET});
    } else {
        add_instruction(builder, (instruction_t){.opcode = ILOAD32, .arg1 = 7654321});
        add_instruction(builder, (instruction_t){.opcode = ARRAY, .arg1 = 1});
        /* A second ARRAY must not consume the request twice. */
        add_instruction(builder, (instruction_t){.opcode = ARRAY, .arg1 = 1});
        if (throwing) {
            add_instruction(builder, (instruction_t){.opcode = THROW});
        } else {
            add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = observer});
            add_instruction(builder, (instruction_t){.opcode = CALL});
        }
        add_instruction(builder, (instruction_t){.opcode = END});
    }
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

static bool run_case(bool exclusive,
                     bool recursive_lock,
                     bool nested,
                     bool no_array,
                     bool throwing,
                     bool pending) {
    size_t baseline = get_allocated_memory_size();
    process_t *proc = create_process();
    gc_stats_t stats;
    CHECK(configure_gc_statistics(proc, &stats));
    bytecode_t *code = program(proc, nested, no_array, throwing);
    /* A dead cycle proves that the admitted pass actually sweeps before the next opcode. */
    object_t *dead = create_array_object(proc, (object_array_t){0});
    operation_result_t inserted = insert_array_elements(dead, 0, &dead, 1);
    CHECK(!inserted.is_exception);
    DECREF(inserted.value);
    DECREF(dead);
    bool admitted = exclusive && !recursive_lock && !nested && !no_array && !pending;
    expected_passes = admitted ? 1 : 0;
    expected_depth = recursive_lock ? 2 : 1;
    gc_dispatch_test_t request = {.exclusive_model_client = exclusive};
    if (pending)
        proc->main_thread->context->pending_flow = FLOW_RETURN;
    if (recursive_lock)
        process_execution_enter(proc);
    int status = run_with_gc_dispatch_test(proc, code, &request);
    CHECK(os_mutex_depth(proc->execution_mutex) == (recursive_lock ? 1 : 0));
    if (recursive_lock)
        process_execution_leave(proc);
    CHECK(request.attempted == !no_array && request.collected == admitted);
    CHECK(stats.collections == expected_passes + 1 && stats.run_end_collections == 1
          && stats.explicit_collections == expected_passes);
    if (throwing) {
        CHECK(status != 0 && proc->main_thread->exception.value);
        CHECK(array_payload(proc->main_thread->exception.value));
    } else {
        CHECK(status == 0);
        if (!no_array)
            CHECK(peek_object_from_stack(proc->main_thread->data_stack, 0)
                  == get_boolean_object(true));
    }
    if (no_array) {
        /* Reuse this process: the declined/unused request is not stored for a later normal run. */
        free_bytecode(code);
        code = program(proc, false, false, false);
        proc->main_thread->instr_id = 0;
        CHECK(configure_gc_statistics(proc, &stats));
        CHECK(run(proc, code) == 0);
        CHECK(!request.attempted && !request.collected);
        CHECK(stats.explicit_collections == 0 && stats.run_end_collections == 1);
        CHECK(peek_object_from_stack(proc->main_thread->data_stack, 0) == get_boolean_object(true));
    }
    destroy_process(proc);
    free_bytecode(code);
    CHECK(get_allocated_memory_size() == baseline);
    return true;
}

static bool rejected_run(void) {
    process_t *proc = create_process();
    bytecode_t *code = program(proc, false, true, false);
    gc_dispatch_test_t request = {true, true, true};
    proc->native_failed = true;
    CHECK(run_with_gc_dispatch_test(proc, code, &request) != 0);
    CHECK(!request.attempted && !request.collected && !os_mutex_depth(proc->execution_mutex));
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

int main(int argc, char **argv) {
    if (argc == 2) {
        bool joined = !strncmp(argv[1], "joined-", 7);
        bool exceptional = strstr(argv[1], "exception") != NULL;
        if (strcmp(argv[1], "parked-return") && strcmp(argv[1], "parked-exception")
            && strcmp(argv[1], "joined-return") && strcmp(argv[1], "joined-exception"))
            return EXIT_FAILURE;
        return test_gc_worker_admission(joined, exceptional) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc != 1)
        return EXIT_FAILURE;
    bool ok = run_case(true, false, false, false, false, false)
              && run_case(false, false, false, false, false, false)
              && run_case(true, true, false, false, false, false)
              && run_case(true, false, true, false, false, false)
              && run_case(true, false, false, true, false, false)
              && run_case(true, false, false, false, true, false)
              && run_case(true, false, false, false, false, true) && rejected_run();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
