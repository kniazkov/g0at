/** @file gc_runtime_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Forced collection through the production dispatcher, without a test VM.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#define GOAT_NATIVE_OK GOAT_MODULE_NATIVE_OK
#include "common/module_compilation.h"
#undef GOAT_NATIVE_OK
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/module_files.h"
#include "model/module_native.h"
#include "unit/analysis_test_support.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void deferred(node_t *node, code_builder_t *builder, data_builder_t *data);

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

static void deferred(node_t *node, code_builder_t *builder, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, builder, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), builder, data);
}

static bool forced;
static bool expect_workers;
static size_t worker_samples;

static operation_result_t sample(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(count == 0);
    process_t *p = thread->process;
    if (forced) {
        if (thread->program_worker)
            ++worker_samples;
        gc_session_t *session = p->gc_session;
        assert(session);
        gc_participant_t *participant = session->participants;
        while (participant && participant->thread != thread)
            participant = participant->next;
        assert(participant && participant->blocked > 0);
        assert(p->gc_statistics->explicit_collections > 0);
        assert(p->gc_statistics->last.after.objects < 100);
        assert(gc_session_request(session));
        assert(gc_session_poll(session) == GC_DEFERRED);
        size_t depth = process_execution_suspend(p);
        process_execution_resume(p, depth);
        assert(gc_session_poll(session) == GC_DEFERRED);
    }
    return operation_success(get_null_object());
}

/* Synchronize through model state, not sleeps: the worker cannot proceed until
   the real main-thread wait has released the model lock. */
static bool terminal_wait;
static uint64_t wait_collections;
static size_t verified_waits;

static operation_result_t await_main(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(count == 0 && thread->program_worker);
    process_t *p = thread->process;
    while (terminal_wait ? !p->main_thread->finished : !p->main_thread->waiting_for) {
        size_t depth = process_execution_suspend(p);
        process_execution_resume(p, depth);
    }
    if (forced) {
        gc_session_t *session = p->gc_session;
        gc_participant_t *main = session->participants;
        while (main && main->thread != p->main_thread)
            main = main->next;
        assert(main && main->published && !main->blocked);
    }
    wait_collections = p->gc_statistics->explicit_collections;
    return operation_success(get_null_object());
}

static operation_result_t verify_wait(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(count == 0 && thread->program_worker);
    process_t *p = thread->process;
    assert(terminal_wait ? p->main_thread->finished : p->main_thread->waiting_for == thread);
    if (forced) {
        assert(p->gc_statistics->explicit_collections > wait_collections + 10);
        assert(p->gc_statistics->last.after.objects < 100);
    } else {
        assert(p->gc_statistics->explicit_collections == 0);
    }
    ++verified_waits;
    return operation_success(get_null_object());
}

static void install_wait_functions(process_t *p) {
    static const builtin_function_t descriptors[] = {
        {.name = U"awaitMain", .execute = await_main},
        {.name = U"verifyWait", .execute = verify_wait}};
    static builtin_function_object_t storage[2];
    static object_static_string_t keys[2];
    for (size_t i = 0; i < 2; ++i)
        create_object_property(p->main_thread->context->data,
                               get_static_string_object(&keys[i], descriptors[i].name),
                               get_builtin_function_object(&storage[i], &descriptors[i]),
                               true);
}

static void check(string_value_t source, bool enabled) {
    size_t before = get_allocated_memory_size();
    bytecode_t *code = compile(source);
    void *image = malloc(code->buffer_size);
    assert(image);
    memcpy(image, code->buffer, code->buffer_size);
    process_t *p = create_process();
    forced = enabled;
    install_wait_functions(p);
    worker_samples = 0;
    static const builtin_function_t descriptor = {.name = U"sample", .execute = sample};
    static builtin_function_object_t storage;
    static object_static_string_t sample_key;
    create_object_property(p->main_thread->context->data,
                           get_static_string_object(&sample_key, U"sample"),
                           get_builtin_function_object(&storage, &descriptor),
                           true);
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    assert(!configure_gc_boundaries(p, enabled, true));
    process_execution_enter(p);
    assert(!configure_gc_boundaries(p, true, false));
    assert(configure_gc_boundaries(p, enabled, true));
    process_execution_leave(p);
    object_t *dead = create_array_object(p, (object_array_t){0});
    operation_result_t inserted = insert_array_elements(dead, 0, &dead, 1);
    assert(!inserted.is_exception);
    DECREF(inserted.value);
    DECREF(dead);
    assert(run(p, code) == 0);
    if (enabled && expect_workers)
        assert(worker_samples == 200);
    assert(!p->gc_session && p->gc_roots_size == 0);
    assert(!p->main_thread->gc_call_participant && !p->main_thread->gc_join_ready);
    assert(stats.run_end_collections == 1);
    assert(enabled ? stats.explicit_collections > 5 : stats.explicit_collections == 0);
    static object_static_string_t key;
    object_t *value = get_object_property(p->main_thread->context->data,
                                          get_static_string_object(&key, U"result"));
    assert(value && get_object_integer_value(value).value == 42);
    assert(memcmp(image, code->buffer, code->buffer_size) == 0);
    free(image);
    destroy_process(p);
    free_bytecode(code);
    assert(get_allocated_memory_size() == before);
}

static void module_case(bool enabled) {
    size_t before = get_allocated_memory_size();
    char *source = "var work=func(){var i=0;while(i<100){"
                   "var a=[];a.push(a);i+=1;}return [21];};"
                   "var a=spawn(work);var b=spawn(work);"
                   "external result=join(a)[0]+join(b)[0];";
    module_source_t file = {.path = "main.goat",
                            .name = "main.goat",
                            .source = source,
                            .length = strlen(source)};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compiled = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compiled, &error));
    process_t *p = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
    assert(prepare_process_launch(p,
                                  module_files_identity(files),
                                  (uint32_t)module_file_id(module_files_main(files)),
                                  "/gc/main.goat"));
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    process_execution_enter(p);
    assert(configure_gc_boundaries(p, enabled, true));
    process_execution_leave(p);
    /* execute_module_file moves this value into a C local while the file runs. */
    p->main_thread->exception.value = create_array_object(p, (object_array_t){0});
    assert(run_module_file(p, module_files_main(files)) == 0);
    assert(!p->gc_session && !p->gc_roots_size);
    assert(enabled ? stats.explicit_collections > 10 : stats.explicit_collections == 0);
    object_static_string_t key = {0};
    object_t *result = get_object_property(module_file_scope(module_files_main(files)),
                                           get_static_string_object(&key, U"result"));
    assert(result && get_object_integer_value(result).value == 42);
    destroy_process(p);
    free_module_compilation(compiled);
    clear_module_compilation_error(&error);
    assert(get_allocated_memory_size() == before);
}

static process_t *adapter_process;
static bool adapter_failure;
static size_t adapter_calls;

static uint32_t GOAT_MODULE_CALL module_adapter(uint32_t abi,
                                                const goat_module_context_v1 *context,
                                                uint32_t count,
                                                const goat_module_value_v1 *args,
                                                goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count == 2);
    assert(args[0].tag == GOAT_MODULE_STRING && args[1].tag == GOAT_MODULE_STRING);
    const goat_module_string_v1 *first = (const void *)(uintptr_t)args[0].payload;
    const goat_module_string_v1 *extra = (const void *)(uintptr_t)args[1].payload;
    assert(first->length == 2 && first->data[0] == 'a' && first->data[1] == 'b');
    assert(extra->length == 2 && extra->data[0] == 'c' && extra->data[1] == 'd');
    assert(!os_mutex_depth(adapter_process->execution_mutex));
    process_execution_enter(adapter_process);
    gc_session_t *session = adapter_process->gc_session;
    assert(session && gc_session_request(session));
    uint64_t before = adapter_process->gc_statistics->explicit_collections;
    assert(gc_session_poll(session) == GC_DEFERRED);
    assert(adapter_process->gc_statistics->explicit_collections == before);
    process_execution_leave(adapter_process);
    assert(first->data[1] == 'b' && extra->data[1] == 'd');
    ++adapter_calls;
    *result = adapter_failure ? (goat_module_value_v1){99, 0, 0} : args[0];
    return GOAT_MODULE_SUCCESS;
}

static bool
resolve_adapter(void *state, uint32_t id, const char *symbol, module_native_entry_t *out) {
    (void)state;
    assert(id == 1 && !strcmp(symbol, "goat_fn_1_v1"));
    *out = (module_native_entry_t){.adapter = module_adapter};
    return true;
}

static void module_native_case(bool failure, bool recursive) {
    size_t before = get_allocated_memory_size();
    char *source = "external f=func(x){return x;};external result=f(\"a\"+\"b\",\"c\"+\"d\");"
                   "if(result!=\"ab\")throw 42;";
    module_source_t file = {.path = "main.goat",
                            .name = "main.goat",
                            .source = source,
                            .length = strlen(source)};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compiled = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compiled, &error));
    const char variant_text[] =
        "{\"goatAbiVersion\":1,\"specializations\":[{\"functionId\":1,"
        "\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
        "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[\"string\"],"
        "\"result\":\"string\"},\"when\":{\"argumentTypes\":[\"string\"]}}]}";
    yyjson_doc *variant = yyjson_read(variant_text, sizeof(variant_text) - 1, 0);
    assert(variant);
    process_t *p = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
    assert(bind_module_files_native(files,
                                    yyjson_doc_get_root(variant),
                                    true,
                                    resolve_adapter,
                                    NULL,
                                    NULL));
    yyjson_doc_free(variant);
    assert(prepare_process_launch(p,
                                  module_files_identity(files),
                                  (uint32_t)module_file_id(module_files_main(files)),
                                  "/gc/main.goat"));
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    process_execution_enter(p);
    assert(configure_gc_boundaries(p, true, true));
    if (!recursive)
        process_execution_leave(p);
    adapter_process = p;
    adapter_failure = failure;
    adapter_calls = 0;
    assert((run_module_file(p, module_files_main(files)) != 0) == failure);
    assert(adapter_calls == 1 && p->main_thread->native_retries == 0);
    assert(!p->gc_session && !p->gc_roots_size);
    assert(!p->main_thread->gc_call_participant && !p->main_thread->gc_join_ready);
    assert(os_mutex_depth(p->execution_mutex) == (recursive ? 1 : 0));
    assert(recursive ? stats.explicit_collections == 0 : stats.explicit_collections > 0);
    if (recursive)
        process_execution_leave(p);
    destroy_process(p);
    adapter_process = NULL;
    free_module_compilation(compiled);
    clear_module_compilation_error(&error);
    assert(get_allocated_memory_size() == before);
}

int main(void) {
    module_native_case(false, false);
    module_native_case(true, false);
    module_native_case(false, true);
    module_native_case(true, true);
    module_case(false);
    module_case(true);
    const string_value_t cases[] = {
        STATIC_STRING(
            U"var work=func(){var i=0;while(i<100){var a=[];a.push(a);sample();i+=1;}return "
            U"[21];};var a=spawn(work);var b=spawn(work);var i=0;while(i<100){i+=1;}var "
            U"result=join(a)[0]+join(b)[0];"),
        STATIC_STRING(U"var f=func(x){return [x+1];}; var result=f(41)[0];"),
        STATIC_STRING(U"var f=func(x){if(x==0){return 42;} return f(x-1);}; var result=f(20);"),
        STATIC_STRING(U"var a=[40]; var f=func(){a[0]+=2;return a[0];}; var result=f();"),
        STATIC_STRING(U"var result=0; var i=0; while(i<500){var a=[]; a.push(a); sample(); i+=1;} "
                      U"result=42;"),
        STATIC_STRING(U"var f=func(){try{return [42];}finally{var x=[1,2];}}; var result=f()[0];"),
        STATIC_STRING(U"var t=spawn(func(){return [42];}); var result=join(t)[0];"),
        STATIC_STRING(U"var result=0; try{throw [42];}catch(e){result=e[0];}finally{var a=[1];}")};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        expect_workers = i == 0;
        check(cases[i], false);
        check(cases[i], true);
    }
    const string_value_t waits[] = {
        STATIC_STRING(U"var t=spawn(func(){awaitMain();var i=0;while(i<100){"
                      U"var a=[];a.push(a);i+=1;}verifyWait();return [42];});"
                      U"var result=join(t)[0];"),
        STATIC_STRING(U"var result=0;try{join(spawn(func(){awaitMain();var i=0;"
                      U"while(i<100){var a=[];a.push(a);i+=1;}verifyWait();throw [42];}));}"
                      U"catch(e){result=e[0];}"),
        STATIC_STRING(U"var result=42;spawn(func(){awaitMain();var i=0;while(i<100){"
                      U"var a=[];a.push(a);i+=1;}verifyWait();});")};
    expect_workers = false;
    for (size_t i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
        terminal_wait = i == 2;
        verified_waits = 0;
        check(waits[i], false);
        check(waits[i], true);
        assert(verified_waits == 2);
    }
    return 0;
}
