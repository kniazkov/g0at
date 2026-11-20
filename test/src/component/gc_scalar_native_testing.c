/** @file gc_scalar_native_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Scalar ABI string ownership at a controlled single-thread collection boundary.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "gc_scalar_native_fixture.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/array.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static process_t *process;
static size_t probes;
static bool expect_host_exception;

static void GOAT_NATIVE_CALL collect_in_adapter(void) {
    /* Only this test host may make the single-client promise; this is not a public callback. */
    assert(process->active_threads == 1 && os_mutex_depth(process->execution_mutex) == 0);
    process_execution_enter(process);
    assert(os_mutex_depth(process->execution_mutex) == 1);
    gc_stats_t *stats = process->gc_statistics;
    assert(stats->collections == 0);
    if (expect_host_exception)
        assert(process->main_thread->exception.value == get_exception_invalid_argument());
    collect_garbage(process);
    if (expect_host_exception)
        assert(process->main_thread->exception.value == get_exception_invalid_argument());
    assert(stats->explicit_collections == 1 && stats->run_end_collections == 0);
    assert(stats->last.before.objects > stats->last.after.objects);
    ++probes;
    process_execution_leave(process);
    assert(os_mutex_depth(process->execution_mutex) == 0);
}

static void deferred(node_t *node, code_builder_t *builder, data_builder_t *data);

static bytecode_t *compile(void) {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node = parse_analysis_test_program(
        &memory,
        STATIC_STRING(U"var f=func(s){return s;};"
                      U"var result=f(\"left\"+\"\U0001F980right\",\"extra\"+\"-value\");"));
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

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *modes[] =
        {"alias", "substring", "copy", "failure", "retry", "host-exception", "host-bad-status"};
    unsigned mode = 0;
    while (mode < 7 && strcmp(argv[2], modes[mode]))
        ++mode;
    assert(mode < 7);
    size_t lock_depth = (size_t)strtoul(argv[3], NULL, 10);
    assert(lock_depth == 0 || lock_depth == 2);
    expect_host_exception = mode >= 5;
    size_t baseline = get_allocated_memory_size();
    native_library_result_t loaded = load_native_library(argv[1]);
    assert(loaded.status == NATIVE_LIBRARY_OK);
    bytecode_t *code = compile();
    assert(code && code->function_count == 1);
    native_function_descriptor_t *descriptor = create_native_function_descriptor(loaded.library, 1);
    assert(descriptor
           && bind_bytecode_native_function(code, code->functions[0].instruction, descriptor));
    release_native_function_descriptor(descriptor);
    void *image = malloc(code->buffer_size);
    assert(image);
    memcpy(image, code->buffer, code->buffer_size);
    process = create_process();
    gc_stats_t stats;
    assert(configure_gc_statistics(process, &stats));
    object_t *dead = create_array_object(process, (object_array_t){0});
    operation_result_t inserted = insert_array_elements(dead, 0, &dead, 1);
    assert(!inserted.is_exception);
    DECREF(inserted.value);
    DECREF(dead);
    size_t roots = process->gc_roots_size;
    gc_scalar_configure(mode, collect_in_adapter);
    /* Exercise suspension/restoration of recursive ownership as well as the normal run lock. */
    for (size_t i = 0; i < lock_depth; ++i)
        process_execution_enter(process);
    int status = run(process, code);
    assert(os_mutex_depth(process->execution_mutex) == lock_depth);
    for (size_t i = 0; i < lock_depth; ++i)
        process_execution_leave(process);
    assert((status != 0) == (mode == 3 || mode >= 5));
    assert(probes == 1 && stats.explicit_collections == 1 && stats.run_end_collections == 1);
    thread_t *thread = process->main_thread;
    assert(thread->native_attempts == 1 && thread->native_retries == (mode == 4));
    assert(thread->native_successes == (mode < 3));
    assert(thread->native_status
           == (mode == 3 || mode == 6 ? GOAT_NATIVE_BAD_REQUEST : GOAT_NATIVE_OK));
    if (expect_host_exception) {
        assert(thread->exception.value == (mode == 5 ? get_exception_invalid_argument() : NULL));
        assert(exception_trace_size(thread->exception.trace) > 0);
        assert(exception_trace_frame(thread->exception.trace, 0)->native_entry);
    }
    if (mode < 3 || mode == 4) {
        object_t *key = create_string_object(process, STATIC_STRING(U"result"));
        object_t *value = get_object_property(thread->context->data, key);
        DECREF(key);
        assert(value && value->vtbl->type == TYPE_STRING);
        string_value_t text = convert_object_to_string(value);
        const gchar_t *expected = mode == 1 ? U"\U0001F980right" : U"left\U0001F980right";
        assert(text.length == STRLEN(expected) && !MEMCMP(text.data, expected, text.length));
        FREE_STRING(text);
    }
    assert(process->gc_roots_size == roots);
    assert(!memcmp(image, code->buffer, code->buffer_size));
    free(image);
    gc_scalar_configure(0, NULL);
    destroy_process(process);
    free_bytecode(code);
    destroy_native_library_result(&loaded);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
