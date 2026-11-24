/** @file gc_acceptance_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Mixed bounded-graph acceptance through production GC boundaries.
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
#include "model/module_files.h"
#include "model/module_native.h"
#include "unit/analysis_test_support.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static process_t *process;
static size_t checkpoints, native_calls;
static bool native_failure;
static uint64_t previous_collection;
static gc_memory_sample_t warm, peak;
static size_t peak_roots, peak_capacity;

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

static operation_result_t await_join(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->program_worker);
    while (process->main_thread->waiting_for != thread) {
        size_t depth = process_execution_suspend(process);
        process_execution_resume(process, depth);
    }
    gc_session_t *s = process->gc_session;
    gc_participant_t *main = s->participants;
    while (main && main->thread != process->main_thread)
        main = main->next;
    assert(main && main->published && !main->blocked);
    return operation_success(get_null_object());
}

static operation_result_t checkpoint(object_t **args, uint16_t count, thread_t *thread) {
    assert(count == 1 && thread->program_worker);
    object_array_t live = get_array_elements(args[0]);
    assert(live.size == 2 && live.items[1] == args[0]);
    assert(get_object_integer_value(live.items[0]).value == 42);
    gc_session_t *s = process->gc_session;
    gc_stats_t *stats = process->gc_statistics;
    assert(s && stats->explicit_collections > previous_collection + 10);
    assert(!stats->last.run_end);
    previous_collection = stats->explicit_collections;
    gc_memory_sample_t sample = stats->last.after;
    /* Fixed-size graph, a single worker and bounded lexical depth. These are
       generous payload budgets, not RSS or cross-platform latency promises. */
    assert(sample.objects < 512 && process->objects.size < 512);
    assert(sample.active_bytes < 256 * 1024);
    assert(sample.auxiliary_bytes < 32 * 1024);
    assert(sample.pooled_bytes <= GOAT_POOL_BYTE_LIMIT);
    assert(process->pooled_bytes <= GOAT_POOL_BYTE_LIMIT);
    assert(process->gc_roots_size < 64 && process->gc_roots_capacity <= 64);
    if (++checkpoints == 16)
        warm = sample;
    if (checkpoints > 16) {
        assert(sample.objects <= warm.objects + 16);
        assert(sample.active_bytes <= warm.active_bytes + 16 * 1024);
        assert(sample.pooled_bytes <= warm.pooled_bytes + 16 * 1024);
        assert(sample.auxiliary_bytes <= warm.auxiliary_bytes + 1024);
    }
#define PEAK(field)                                                                                \
    if (sample.field > peak.field)                                                                 \
    peak.field = sample.field
    PEAK(objects);
    PEAK(active_bytes);
    PEAK(pooled_bytes);
    PEAK(auxiliary_bytes);
#undef PEAK
    if (process->gc_roots_size > peak_roots)
        peak_roots = process->gc_roots_size;
    if (process->gc_roots_capacity > peak_capacity)
        peak_capacity = process->gc_roots_capacity;
    /* Ordinary callbacks must not publish, including after releasing the lock.
       The next checkpoint proves this request was serviced after returning. */
    assert(!thread->gc_call_participant && !gc_call_wait_begin(thread));
    assert(gc_session_request(s));
    for (unsigned i = 0; i < 8; ++i) {
        size_t depth = process_execution_suspend(process);
        process_execution_resume(process, depth);
        assert(gc_session_poll(s) == GC_DEFERRED && s->requested);
        assert(stats->explicit_collections == previous_collection);
    }
    assert(get_array_elements(args[0]).items[1] == args[0]);
    return operation_success(get_null_object());
}

static uint32_t GOAT_MODULE_CALL adapter(uint32_t abi,
                                         const goat_module_context_v1 *context,
                                         uint32_t count,
                                         const goat_module_value_v1 *args,
                                         goat_module_value_v1 *result) {
    (void)context;
    assert(abi == GOAT_MODULE_ABI_VERSION && count == 1);
    assert(args[0].tag == GOAT_MODULE_STRING);
    const goat_module_string_v1 *text = (const void *)(uintptr_t)args[0].payload;
    assert(text->length == 2 && text->data[0] == 'a' && text->data[1] == 'b');
    assert(!os_mutex_depth(process->execution_mutex));
    process_execution_enter(process);
    gc_session_t *s = process->gc_session;
    assert(gc_session_request(s));
    assert(gc_session_poll(s) == GC_COLLECTED);
    ++native_calls;
    process_execution_leave(process);
    assert(text->length == 2 && text->data[1] == 'b');
    *result = native_failure && native_calls == 128 ? (goat_module_value_v1){99, 0, 0} : args[0];
    return GOAT_MODULE_SUCCESS;
}

static bool resolve(void *state, uint32_t id, const char *symbol, module_native_entry_t *out) {
    (void)state;
    assert(id == 1 && !strcmp(symbol, "goat_fn_1_v1"));
    *out = (module_native_entry_t){.adapter = adapter};
    return true;
}

static void clean_run_state(void) {
    assert(!process->gc_session && !process->execution && !process->gc_roots_size);
    assert(!process->active_threads && !process->finished_workers);
    assert(!process->initialization_count && !process->initialization_waiters);
    thread_t *t = process->main_thread;
    do {
        assert(!t->gc_call_participant && !t->gc_join_ready);
        t = t->next;
    } while (t != process->main_thread);
    assert(!os_mutex_depth(process->execution_mutex));
}

int main(int argc, char **argv) {
    assert(argc == 2
           && (!strcmp(argv[1], "vm") || !strcmp(argv[1], "native")
               || !strcmp(argv[1], "native-error")));
    native_failure = !strcmp(argv[1], "native-error");
    bool native = strcmp(argv[1], "vm") != 0;
    size_t before = get_allocated_memory_size();
    char *source =
        "external bridge=func(x){return x;};"
        "external work=func(fail){"
        "var t=spawn(func(){awaitJoin();var live=[42];live.push(live);"
        "var state={var made=0;};var finalized=0;"
        "var Proto={const init=func(){this.value=[42];state.made+=1;};};"
        "var nested=func(x){try{return [x];}finally{var a=[];a.push(a);finalized+=1;}};"
        "var i=0;while(i<256){"
        "var dead=[];dead.push(dead);var obj=Proto->{};"
        "var value=nested(obj.value[0]);"
        "try{throw value;}catch(e){if(e[0]!=42){throw 1;}}"
        "finally{var a=[];a.push(a);}"
        "if(bridge(\"a\"+\"b\")!=\"ab\"){throw 2;}"
        "if(obj.clone().value[0]!=42||obj.flat().value[0]!=42){throw 3;}"
        "checkpoint(live);i+=1;}"
        "if(state.made!=256||finalized!=256||live[0]!=42){throw [state.made,finalized,live[0]];}"
        "if(fail){throw [42];}return [42];});"
        "return join(t);};";
    module_source_t input = {.path = "main.goat",
                             .name = "main.goat",
                             .source = source,
                             .length = strlen(source)};
    module_sources_t sources = {.files = &input, .count = 1};
    module_compilation_t *module = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &module, &error));
    clear_module_compilation_error(&error);
    void *module_image = malloc(module->code->buffer_size);
    assert(module_image);
    memcpy(module_image, module->code->buffer, module->code->buffer_size);
    process = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(process, module->metadata, module->code, &files));
    module_file_t *file = module_files_main(files);
    object_t *scope = module_file_scope(file);
    static const builtin_function_t functions[] = {{.name = U"awaitJoin", .execute = await_join},
                                                   {.name = U"checkpoint", .execute = checkpoint}};
    static builtin_function_object_t storage[2];
    static object_static_string_t keys[2], suite_key;
    for (size_t i = 0; i < 2; ++i)
        assert(create_object_property(scope,
                                      get_static_string_object(&keys[i], functions[i].name),
                                      get_builtin_function_object(&storage[i], &functions[i]),
                                      true)
               == MSTAT_OK);
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
    }
    gc_stats_t stats;
    assert(configure_gc_statistics(process, &stats));
    process_execution_enter(process);
    assert(configure_gc_boundaries(process, true, true));
    process_execution_leave(process);
    assert(!run_module_file(process, file));
    clean_run_state();
    assert(create_object_property(process->main_thread->context->data,
                                  get_static_string_object(&suite_key, U"suite"),
                                  scope,
                                  true)
           == MSTAT_OK);
    const string_value_t programs[] = {
        STATIC_STRING(U"var result=suite.work(false);if(result[0]!=42){throw 5;}"),
        STATIC_STRING(U"var caught=false;try{suite.work(true);}catch(e){"
                      U"if(e[0]!=42){throw 7;}caught=true;}if(!caught){throw 8;}"),
        STATIC_STRING(U"result=suite.work(false);if(result[0]!=42){throw 6;}"),
        STATIC_STRING(U"suite.work(true);")};
    for (size_t run_index = 0; run_index < (native_failure ? 1 : 4); ++run_index) {
        bytecode_t *code = compile(programs[run_index]);
        void *image = malloc(code->buffer_size);
        assert(image);
        memcpy(image, code->buffer, code->buffer_size);
        checkpoints = native_calls = previous_collection = 0;
        warm = peak = (gc_memory_sample_t){0};
        peak_roots = peak_capacity = 0;
        assert(configure_gc_statistics(process, &stats));
        // The embedding caller selects the entry instruction for each new image.
        process->main_thread->instr_id = 0;
        int status = run(process, code);
        if (status && !native_failure && run_index != 3 && process->main_thread->exception.value) {
            string_value_t message =
                convert_object_to_string(process->main_thread->exception.value);
            fprintf(stderr, "status=%d checkpoints=%zu exception=", status, checkpoints);
            for (size_t i = 0; i < message.length; ++i)
                fputc((char)message.data[i], stderr);
            fputc(10, stderr);
            FREE_STRING(message);
        }
        assert((status != 0) == (native_failure || run_index == 3));
        assert(checkpoints == (native_failure ? 127 : 256));
        assert(native_calls == (native ? (native_failure ? 128 : 256) : 0));
        assert(stats.explicit_collections > previous_collection);
        assert(stats.run_end_collections == 1);
        clean_run_state();
        if (native_failure) {
            assert(process->native_failed);
            thread_t *t = process->main_thread;
            do {
                assert(!t->native_retries);
                t = t->next;
            } while (t != process->main_thread);
            uint64_t collections = stats.collections;
            assert(run(process, code) != 0); // terminal backend failure never replays
            assert(native_calls == 128 && checkpoints == 127);
            assert(stats.collections == collections);
            clean_run_state();
        }
        if (run_index == 3) {
            object_array_t thrown = get_array_elements(process->main_thread->exception.value);
            assert(thrown.size == 1 && get_object_integer_value(thrown.items[0]).value == 42);
            clear_exception(&process->main_thread->exception);
        }
        printf("%s run=%zu checkpoints=%zu collections=%llu objects=%zu active=%zu pool=%zu "
               "auxiliary=%zu roots=%zu capacity=%zu\n",
               argv[1],
               run_index,
               checkpoints,
               (unsigned long long)stats.explicit_collections,
               peak.objects,
               peak.active_bytes,
               peak.pooled_bytes,
               peak.auxiliary_bytes,
               peak_roots,
               peak_capacity);
        assert(!memcmp(image, code->buffer, code->buffer_size));
        free(image);
        free_bytecode(code);
    }
    assert(!memcmp(module_image, module->code->buffer, module->code->buffer_size));
    free(module_image);
    destroy_process(process);
    free_module_compilation(module);
    assert(get_allocated_memory_size() == before);
    return 0;
}
