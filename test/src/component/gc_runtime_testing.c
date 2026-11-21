/** @file gc_runtime_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Forced collection through the production dispatcher, without a test VM.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
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

static operation_result_t sample(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(count == 0);
    process_t *p = thread->process;
    if (forced) {
        gc_session_t *session = p->gc_session;
        assert(session && session->participants->blocked > 0);
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

static void check(string_value_t source, bool enabled) {
    size_t before = get_allocated_memory_size();
    bytecode_t *code = compile(source);
    void *image = malloc(code->buffer_size);
    assert(image);
    memcpy(image, code->buffer, code->buffer_size);
    process_t *p = create_process();
    forced = enabled;
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
    assert(!p->gc_session && p->gc_roots_size == 0);
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

int main(void) {
    const string_value_t cases[] = {
        STATIC_STRING(U"var f=func(x){return [x+1];}; var result=f(41)[0];"),
        STATIC_STRING(U"var f=func(x){if(x==0){return 42;} return f(x-1);}; var result=f(20);"),
        STATIC_STRING(U"var a=[40]; var f=func(){a[0]+=2;return a[0];}; var result=f();"),
        STATIC_STRING(U"var result=0; var i=0; while(i<500){var a=[]; a.push(a); sample(); i+=1;} "
                      U"result=42;"),
        STATIC_STRING(U"var f=func(){try{return [42];}finally{var x=[1,2];}}; var result=f()[0];"),
        STATIC_STRING(U"var t=spawn(func(){return [42];}); var result=join(t)[0];"),
        STATIC_STRING(U"var result=0; try{throw [42];}catch(e){result=e[0];}finally{var a=[1];}")};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        check(cases[i], false);
        check(cases[i], true);
    }
    return 0;
}
