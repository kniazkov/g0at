/** @file gc_policy_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Allocation accounting, adaptive budgets and ordinary runtime admission.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/memory/allocate.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/module_native.h"
#include "model/string_builder.h"
#include "unit/analysis_test_support.h"
#include "vm/gc_policy.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void formula(void) {
    process_t *p = create_process();
    assert(!p->gc_policy.enabled);
    assert(!configure_gc_policy(p, 1024, true));
    process_execution_enter(p);
    assert(!configure_gc_policy(p, 1024, false));
    assert(configure_gc_policy(p, 1024, true));
    gc_policy_t *policy = &p->gc_policy;
    gc_policy_collected(p, 8000, 7000); // exactly 1/8 useful
    assert(policy->threshold_bytes == 3500 && !policy->backoff);
    for (unsigned i = 1; i <= 6; ++i) {
        gc_policy_collected(p, 8000, 8000);
        unsigned backoff = i < GC_POLICY_MAX_BACKOFF ? i : GC_POLICY_MAX_BACKOFF;
        assert(policy->backoff == backoff);
        assert(policy->threshold_bytes == (size_t)4000 << backoff);
    }
    gc_policy_collected(p, 16000, 8000);
    assert(policy->backoff == 3 && policy->threshold_bytes == 32000);
    for (unsigned i = 0; i < 4; ++i)
        gc_policy_collected(p, 7, 6); // upward-rounded usefulness, and minimum budget
    assert(!policy->backoff && policy->threshold_bytes == 1024);
    gc_policy_note_allocation(p, 1023);
    assert(!policy->pending);
    gc_policy_note_allocation(p, 0);
    assert(policy->allocation_events == 1);
    gc_policy_note_allocation(p, 1);
    assert(policy->pending);
    gc_policy_note_allocation(p, SIZE_MAX);
    assert(policy->allocated_bytes == SIZE_MAX);
    policy->allocation_events = policy->collections = SIZE_MAX;
    gc_policy_note_allocation(p, 1);
    gc_policy_collected(p, SIZE_MAX, SIZE_MAX);
    assert(policy->allocation_events == SIZE_MAX && policy->collections == SIZE_MAX);
    assert(!policy->pending && !policy->allocated_bytes);
    gc_policy_collected(p, SIZE_MAX, SIZE_MAX);
    assert(policy->threshold_bytes == SIZE_MAX);
    assert(configure_gc_policy(p, SIZE_MAX, true));
    gc_policy_note_allocation(p, SIZE_MAX - 1);
    assert(!policy->pending);
    gc_policy_note_allocation(p, 1);
    assert(policy->pending && policy->threshold_bytes == SIZE_MAX);
    assert(configure_gc_policy(p, 0, false));
    gc_policy_note_allocation(p, SIZE_MAX);
    assert(!policy->enabled && !policy->allocated_bytes && !policy->pending);
    process_execution_leave(p);
    destroy_process(p);
}

static void accounting(void) {
    process_t *p = create_process(), *other = create_process();
    process_execution_enter(p);
    assert(configure_gc_policy(p, SIZE_MAX, true));
    gc_policy_t *policy = &p->gc_policy;
    object_t *integer = create_integer_object(p, 123456);
    size_t shell = integer->vtbl->gc_owned_payload(integer);
    assert(policy->allocated_bytes == shell && policy->allocation_events == 1);
    DECREF(integer);
    integer = create_integer_object(p, 123457); // activation from pool still counts
    assert(policy->allocated_bytes == 2 * shell);
    DECREF(integer);
    size_t before = policy->allocated_bytes;
    object_t *foreign = create_integer_object(other, 123456);
    DECREF(foreign);
    assert(policy->allocated_bytes == before && !other->gc_policy.allocation_events);
    object_t *array = create_array_object(p, (object_array_t){0});
    size_t payload = array->vtbl->gc_owned_payload(array);
    assert(policy->allocated_bytes == before + payload);
    object_t *value = get_null_object();
    before = policy->allocated_bytes;
    operation_result_t r = insert_array_elements(array, 0, &value, 1);
    assert(!r.is_exception);
    DECREF(r.value);
    assert(policy->allocated_bytes == before + array->vtbl->gc_owned_payload(array) - payload);
    payload = array->vtbl->gc_owned_payload(array);
    before = policy->allocated_bytes;
    r = insert_array_elements(array, 1, &value, 1); // spare capacity, static small length
    assert(!r.is_exception);
    DECREF(r.value);
    assert(policy->allocated_bytes == before && array->vtbl->gc_owned_payload(array) == payload);
    DECREF(array);
    object_t *root = get_root_object();
    object_t *user = create_user_defined_object(p, (object_array_t){&root, 1});
    before = policy->allocated_bytes;
    payload = gc_policy_object_bytes(user);
    object_static_string_t key = {0};
    assert(create_object_property(user, get_static_string_object(&key, U"field"), value, false)
           == MSTAT_OK);
    assert(policy->allocated_bytes == before + gc_policy_object_bytes(user) - payload);
    before = policy->allocated_bytes;
    payload = gc_policy_object_bytes(user);
    initialize_string_builder_storage(user);
    assert(policy->allocated_bytes == before + gc_policy_object_bytes(user) - payload);
    assert(create_object_property(user, get_string_length(), get_integer_zero(), true) == MSTAT_OK);
    object_static_string_t text_storage = {0};
    object_t *text = get_static_string_object(&text_storage, U"growth");
    before = policy->allocated_bytes;
    payload = gc_policy_object_bytes(user);
    const builtin_function_t *append =
        ((builtin_function_object_t *)get_string_builder_append_function())->descriptor;
    r = append->execute_method(user, &text, 1, p->main_thread);
    assert(!r.is_exception);
    DECREF(r.value);
    assert(gc_policy_object_bytes(user) > payload);
    assert(policy->allocated_bytes == before + gc_policy_object_bytes(user) - payload);
    object_t *copy = create_user_defined_object(p, (object_array_t){&root, 1});
    before = policy->allocated_bytes;
    payload = gc_policy_object_bytes(copy);
    copy_string_builder_storage(copy, user);
    assert(policy->allocated_bytes == before + gc_policy_object_bytes(copy) - payload);
    assert(get_string_builder_storage(copy)->length == 6);
    DECREF(copy);
    DECREF(user);
    // Real large live storage raises the initial budget without tracing or telemetry.
    object_t **items = ALLOC(8192 * sizeof(*items));
    for (size_t i = 0; i < 8192; ++i)
        items[i] = value;
    array = create_array_object(p, (object_array_t){items, 8192});
    FREE(items);
    assert(configure_gc_policy(p, 1024, true));
    size_t live = gc_measure_memory(p).active_bytes;
    assert(policy->live_bytes == live && policy->threshold_bytes == live / 2);
    assert(policy->threshold_bytes >= 4096 * sizeof(void *));
    gc_root_handle_t handle = register_gc_root(p, array);
    assert(handle.serial);
    gc_session_t session = {0};
    gc_participant_t token = {0};
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, p->main_thread, &token));
    for (unsigned i = 0; i < 16384; ++i) {
        integer = create_integer_object(p, 123456);
        DECREF(integer);
        assert(gc_participant_publish(&token));
        gc_poll_result_t outcome = gc_session_poll(&session);
        assert(outcome == GC_IDLE || outcome == GC_COLLECTED);
        assert(gc_participant_withdraw(&token));
    }
    assert(policy->collections >= 3 && policy->collections <= 8);
    assert(policy->backoff >= 3 && policy->threshold_bytes >= live * 4);
    assert(get_array_elements(array).size == 8192);
    printf("live-set bytes=%zu collections=%zu backoff=%u budget=%zu events=%zu\n",
           live,
           policy->collections,
           policy->backoff,
           policy->threshold_bytes,
           policy->allocation_events);
    assert(gc_participant_leave(&token) && gc_session_end(&session));
    assert(unregister_gc_root(p, handle));
    DECREF(array);
    process_execution_leave(p);
    destroy_process(p);
    destroy_process(other);
}

static void deferred_request(void) {
    process_t *p = create_process();
    process_execution_enter(p);
    assert(configure_gc_policy(p, 1024, true));
    gc_session_t session = {0};
    gc_participant_t token = {0};
    gc_policy_note_allocation(p, p->gc_policy.threshold_bytes);
    assert(p->gc_policy.pending && !p->gc_policy.collections && !p->gc_session);
    size_t debt = p->gc_policy.allocated_bytes;
    for (unsigned i = 0; i < 2; ++i) {
        assert(gc_session_begin(&session, p, true));
        assert(gc_participant_enter(&session, p->main_thread, &token));
        assert(gc_participant_block(&token));
        assert(!configure_gc_policy(p, 0, false));
        for (unsigned j = 0; j < 8; ++j) {
            assert(gc_session_poll(&session) == GC_DEFERRED);
            assert(session.requested && session.requests == 1 && p->gc_policy.pending);
        }
        assert(p->gc_policy.allocated_bytes == debt && !p->gc_policy.collections);
        assert(gc_participant_unblock(&token));
        assert(gc_participant_leave(&token));
        assert(gc_session_end(&session));
        session = (gc_session_t){0};
    }
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, p->main_thread, &token));
    assert(gc_participant_publish(&token));
    assert(gc_session_poll(&session) == GC_COLLECTED);
    assert(!p->gc_policy.pending && !p->gc_policy.allocated_bytes);
    assert(p->gc_policy.collections == 1 && !p->gc_statistics);
    assert(gc_session_poll(&session) == GC_IDLE);
    // Host requests remain independent of the allocation threshold.
    gc_policy_note_allocation(p, 1);
    assert(!p->gc_policy.pending && gc_session_request(&session));
    assert(gc_session_poll(&session) == GC_COLLECTED);
    assert(!p->gc_policy.allocated_bytes && p->gc_policy.collections == 2);
    assert(gc_participant_withdraw(&token) && gc_participant_leave(&token));
    assert(gc_session_end(&session));
    process_execution_leave(p);
    destroy_process(p);
}

static void pressure(void) {
    process_t *p = create_process();
    assert(!request_gc_pressure(NULL) && !request_gc_pressure(p));
    process_execution_enter(p);
    assert(!request_gc_pressure(p));
    assert(configure_gc_policy(p, SIZE_MAX, true));
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    object_t *value = create_integer_object(p, 123456);
    object_t *live = create_array_object(p, (object_array_t){&value, 1});
    DECREF(value);
    object_t *cycle = create_array_object(p, (object_array_t){0});
    operation_result_t insertion = insert_array_elements(cycle, 0, &cycle, 1);
    assert(!insertion.is_exception);
    DECREF(insertion.value);
    DECREF(cycle);
    gc_root_handle_t root = register_gc_root(p, live);
    assert(root.serial);
    object_t *values[600];
    for (size_t i = 0; i < 600; ++i)
        values[i] = create_integer_object(p, 100000 + i);
    for (size_t i = 0; i < 600; ++i)
        DECREF(values[i]);
    assert(p->pooled_bytes > 10000);
    size_t held = p->pooled_bytes;
    assert(request_gc_pressure(p) && request_gc_pressure(p));
    assert(!configure_gc_policy(p, 0, false));
    gc_session_t session = {0};
    gc_participant_t token = {0};
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, p->main_thread, &token));
    assert(gc_participant_block(&token) && gc_participant_block(&token));
    for (unsigned i = 0; i < 100; ++i)
        assert(gc_session_poll(&session) == GC_DEFERRED);
    assert(p->pooled_bytes == held && !p->gc_pressure.attempts && session.requests == 1);
    assert(gc_participant_unblock(&token));
    assert(gc_session_poll(&session) == GC_DEFERRED);
    assert(gc_participant_unblock(&token) && gc_participant_leave(&token));
    assert(gc_session_end(&session));
    assert(p->gc_pressure.pending);
    session = (gc_session_t){0};
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, p->main_thread, &token));
    assert(gc_participant_publish(&token));
    assert(gc_session_poll(&session) == GC_COLLECTED);
    assert(!p->pooled_bytes && !p->gc_pressure.pending && p->gc_pressure.attempts == 1);
    assert(p->gc_pressure.released_bytes > held);
    assert(stats.trim_calls == 1 && stats.trimmed_bytes >= held);
    assert(stats.last.freed_bytes == p->gc_pressure.released_bytes);
    assert(get_array_elements(live).items[0] == value);
    assert(value->vtbl->get_integer_value(value).value == 123456);
    assert(request_gc_pressure(p));
    assert(gc_session_poll(&session) == GC_COLLECTED);
    assert(p->gc_pressure.attempts == 2 && !p->gc_pressure.released_bytes);
    for (unsigned i = 0; i < 100; ++i)
        assert(gc_session_poll(&session) == GC_IDLE);
    assert(p->gc_pressure.attempts == 2);
    p->gc_collecting = true;
    assert(!request_gc_pressure(p));
    p->gc_collecting = false;
    p->draining_releases = true;
    assert(!request_gc_pressure(p));
    p->draining_releases = false;
    p->gc_pressure.attempts = SIZE_MAX;
    assert(request_gc_pressure(p));
    assert(gc_session_poll(&session) == GC_COLLECTED);
    assert(p->gc_pressure.attempts == SIZE_MAX);
    assert(gc_participant_withdraw(&token) && gc_participant_leave(&token));
    assert(gc_session_end(&session));
    assert(request_gc_pressure(p));
    collect_garbage(p); // final/explicit pass also discharges pressure
    assert(!p->gc_pressure.pending);
    unregister_gc_root(p, root);
    DECREF(live);
    process_execution_leave(p);
    destroy_process(p);
}

static void deferred(node_t *node, code_builder_t *builder, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, builder, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), builder, data);
}

static bytecode_t *compile(string_value_t source) {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node = parse_analysis_test_program(&memory, source);
    assert(node);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, builder, data);
    deferred(node, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    return code;
}

static bool enabled, pressure_runtime;
static size_t observations, previous_collections;

static operation_result_t monitor(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && !thread->process->gc_force_boundaries);
    process_t *p = thread->process;
    if (enabled) {
        assert(p->gc_policy.collections > previous_collections);
        previous_collections = p->gc_policy.collections;
        assert(p->objects.size < 512 && gc_measure_memory(p).active_bytes < 256 * 1024);
        assert(p->pooled_bytes <= GOAT_POOL_BYTE_LIMIT && p->gc_roots_size < 64);
        assert(!configure_gc_policy(p, 0, false));
    } else {
        assert(!p->gc_policy.collections && !p->gc_session);
    }
    if (pressure_runtime) {
        assert(p->gc_pressure.attempts == observations);
        assert(request_gc_pressure(p));
    }
    ++observations;
    return operation_success(get_null_object());
}

static process_t *native_process;
static size_t native_calls;

static uint32_t GOAT_MODULE_CALL adapter(uint32_t abi,
                                         const goat_module_context_v1 *context,
                                         uint32_t count,
                                         const goat_module_value_v1 *args,
                                         goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count == 1 && args[0].tag == GOAT_MODULE_STRING);
    const goat_module_string_v1 *text = (const void *)(uintptr_t)args[0].payload;
    assert(text->length == 2 && text->data[0] == 'a' && text->data[1] == 'b');
    assert(!os_mutex_depth(native_process->execution_mutex));
    process_execution_enter(native_process);
    gc_session_t *session = native_process->gc_session;
    assert(session && native_process->gc_policy.enabled && !native_process->gc_force_boundaries);
    // Poll the genuine request only: this adapter must not force an extra collection.
    gc_poll_result_t outcome = gc_session_poll(session);
    assert(outcome == GC_IDLE || outcome == GC_COLLECTED);
    ++native_calls;
    process_execution_leave(native_process);
    assert(text->length == 2 && text->data[1] == 'b');
    *result = args[0];
    return GOAT_MODULE_SUCCESS;
}

static bool resolve(void *state, uint32_t id, const char *symbol, module_native_entry_t *out) {
    (void)state;
    assert(id == 1 && !strcmp(symbol, "goat_fn_1_v1"));
    *out = (module_native_entry_t){.adapter = adapter};
    return true;
}

static void runtime(bool module, bool telemetry, bool enable, bool native) {
    process_t *p = create_process();
    enabled = enable;
    static const builtin_function_t descriptor = {.name = U"monitor", .execute = monitor};
    static builtin_function_object_t storage;
    static object_static_string_t key;
    object_t *function = get_builtin_function_object(&storage, &descriptor);
    const gchar_t program[] =
        U"{var bridge=func(x){return x;};var worker=func(){var i=0;var alive=[42];while(i<1000){"
        U"var a=[];a.push(a);var b=[];b.push(b);"
        U"if(bridge(\"a\"+\"b\")!=\"ab\")throw 3;"
        U"try{throw [42];}catch(e){if(e[0]!=alive[0])throw 1;}"
        U"finally{var c=[];c.push(c);}"
        U"i+=1;if(i%100==0){monitor();}}return alive;};"
        U"var result=join(spawn(worker));if(result[0]!=42)throw 2;}";
    bytecode_t *code =
        compile((string_value_t){program, sizeof(program) / sizeof(*program) - 1, false});
    module_compilation_t *compiled = NULL;
    module_file_t *file = NULL;
    object_t *scope = p->main_thread->context->data;
    if (module) {
        char *source = encode_utf8(program);
        module_source_t input = {.path = "main.goat",
                                 .name = "main.goat",
                                 .source = source,
                                 .length = strlen(source)};
        module_sources_t sources = {.files = &input, .count = 1};
        module_compilation_error_t error = {0};
        assert(compile_module_sources(&sources, &compiled, &error));
        clear_module_compilation_error(&error);
        FREE(source);
        module_files_t *files = NULL;
        assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
        if (native) {
            const char variant_text[] =
                "{\"goatAbiVersion\":1,\"specializations\":[{\"functionId\":1,"
                "\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
                "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[\"string\"],"
                "\"result\":\"string\"},\"when\":{\"argumentTypes\":[\"string\"]}}]}";
            yyjson_doc *variant = yyjson_read(variant_text, sizeof(variant_text) - 1, 0);
            assert(variant
                   && bind_module_files_native(files,
                                               yyjson_doc_get_root(variant),
                                               false,
                                               resolve,
                                               NULL,
                                               NULL));
            yyjson_doc_free(variant);
            native_process = p;
            native_calls = 0;
        }
        file = module_files_main(files);
        assert(prepare_process_launch(p,
                                      module_files_identity(files),
                                      (uint32_t)module_file_id(file),
                                      "/gc/main.goat"));
        scope = module_file_scope(file);
    }
    assert(create_object_property(scope, get_static_string_object(&key, U"monitor"), function, true)
           == MSTAT_OK);
    gc_stats_t stats;
    if (telemetry)
        assert(configure_gc_statistics(p, &stats));
    process_execution_enter(p);
    assert(configure_gc_policy(p, enable ? 8192 : 0, true));
    process_execution_leave(p);
    unsigned runs = module ? 1 : 2;
    for (unsigned i = 0; i < runs; ++i) {
        observations = 0;
        if (pressure_runtime)
            p->gc_pressure.attempts = 0;
        previous_collections = p->gc_policy.collections;
        size_t before = previous_collections;
        p->main_thread->instr_id = 0;
        assert((module ? run_module_file(p, file) : run(p, code)) == 0);
        assert(observations == 10);
        if (pressure_runtime) {
            assert(p->gc_pressure.attempts == 10 && !p->gc_pressure.pending);
        }
        if (native)
            assert(native_calls == 1000);
        assert(!p->execution && !p->gc_session && !p->gc_roots_size && !p->active_threads);
        assert(!p->gc_policy.pending && !p->gc_policy.allocated_bytes);
        if (enable) {
            size_t passes = p->gc_policy.collections - before;
            assert(passes > 10 && passes < 500); // not one collection per allocation/instruction
            printf("%s telemetry=%d run=%u collections=%zu budget=%zu live=%zu events=%zu\n",
                   module ? "module" : "vm",
                   telemetry,
                   i,
                   passes,
                   p->gc_policy.threshold_bytes,
                   p->gc_policy.live_bytes,
                   p->gc_policy.allocation_events);
        }
        if (telemetry)
            assert(enable ? stats.explicit_collections > 10 : stats.explicit_collections == 0);
    }
    destroy_process(p);
    free_bytecode(code);
    if (compiled)
        free_module_compilation(compiled);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    size_t before = get_allocated_memory_size();
    if (!strcmp(argv[1], "pressure"))
        pressure();
    else if (!strcmp(argv[1], "pressure-vm")) {
        pressure_runtime = true;
        runtime(false, true, true, false);
    } else if (!strcmp(argv[1], "pressure-no-stats")) {
        pressure_runtime = true;
        runtime(false, false, true, false);
    } else if (!strcmp(argv[1], "pressure-native")) {
        pressure_runtime = true;
        runtime(true, true, true, true);
    } else if (!strcmp(argv[1], "formula"))
        formula();
    else if (!strcmp(argv[1], "accounting"))
        accounting();
    else if (!strcmp(argv[1], "deferred"))
        deferred_request();
    else if (!strcmp(argv[1], "vm"))
        runtime(false, true, true, false);
    else if (!strcmp(argv[1], "no-stats"))
        runtime(false, false, true, false);
    else if (!strcmp(argv[1], "module"))
        runtime(true, true, true, false);
    else if (!strcmp(argv[1], "native"))
        runtime(true, true, true, true);
    else if (!strcmp(argv[1], "disabled"))
        runtime(false, true, false, false);
    else
        abort();
    assert(get_allocated_memory_size() == before);
    return 0;
}
