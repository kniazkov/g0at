/** @file gc_module_initialization_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Collection inside nested initializers and competing real VM gate waits.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "common/module_compilation.h"
#include "lib/memory/allocate.h"
#include "model/builtin_function.h"
#include "model/module_files.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool concurrent, recursive, failed, entry;
static size_t observations;
static uint64_t initial_collections;

static operation_result_t await_gate(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->initialization_depth);
    process_t *p = thread->process;
    if (concurrent) {
        while (!p->initialization_waiters)
            wait_os_condition(p->threads_changed, p->execution_mutex);
        gc_session_t *s = p->gc_session;
        assert(s->initialization_waiters == 1 && s->initializers == 1);
        gc_participant_t *owner = s->participants;
        while (owner && owner->thread != thread)
            owner = owner->next;
        assert(owner && owner->blocked == 1 && !owner->published);
        size_t published = 0;
        for (gc_participant_t *t = s->participants; t; t = t->next)
            published += t->published;
        /* One waiter on the initialization gate; main may still be reaching join. */
        assert(published >= 1);
    }
    initial_collections = p->gc_statistics->explicit_collections;
    return operation_success(get_null_object());
}

static operation_result_t probe(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->initialization_depth == 2);
    process_t *p = thread->process;
    assert(p->initialization_count == 2);
    gc_session_t *session = p->gc_session;
    assert(session->initializers == (recursive ? 0 : 2));
    assert(thread->gc_initialization_depth == (recursive ? 0 : 2));
    if (recursive)
        assert(p->gc_statistics->explicit_collections == 0);
    else {
        assert(p->gc_statistics->explicit_collections > initial_collections + 10);
        assert(p->gc_statistics->last.after.objects < 150);
    }
    /* This callback is unreviewed: even a fully counted initializer cannot
       publish arbitrary C locals merely because it owns the model lock. */
    assert(!thread->gc_call_participant);
    assert(gc_session_request(p->gc_session));
    assert(gc_session_poll(p->gc_session) == GC_DEFERRED);
    ++observations;
    return operation_success(get_null_object());
}

int main(int argc, char **argv) {
    assert(argc == 2);
    concurrent = strstr(argv[1], "workers") != NULL;
    recursive = strstr(argv[1], "recursive") != NULL;
    failed = strstr(argv[1], "failure") != NULL;
    entry = strstr(argv[1], "entry") != NULL;
    assert(!concurrent || !recursive);
    size_t baseline = get_allocated_memory_size();
    char *main_source =
        concurrent
            ? (failed ? "var f=func(){return include \"init.goat\";};var a=spawn(f);var b=spawn(f);"
                        "var sum=0;try{join(a);}catch(e){sum+=e[0];}"
                        "try{join(b);}catch(e){sum+=e[0];}external result=sum;"
                      : "var f=func(){return include \"init.goat\";};var a=spawn(f);var b=spawn(f);"
                        "external result=join(a).result+join(b).result;")
            : (failed ? "var sum=0;try{include \"init.goat\";}catch(e){sum=e[0];}"
                        "try{include \"init.goat\";}catch(e){sum+=e[0];}external result=sum;"
                      : "external result=(include \"init.goat\").result*2;");
    char *init_source = "awaitGate();var held=[21];var leaf=include \"leaf.goat\";"
                        "external result=held[0]+leaf.result;";
    char *leaf_source =
        failed ? "var i=0;while(i<100){var a=[];a.push(a);i+=1;}probe();throw [42];"
               : "var i=0;while(i<100){var a=[];a.push(a);i+=1;}probe();external result=21;";
    module_source_t input[] = {{.path = "main.goat", .name = "main.goat", .source = main_source},
                               {.path = "init.goat", .name = "init.goat", .source = init_source},
                               {.path = "leaf.goat", .name = "leaf.goat", .source = leaf_source}};
    size_t targets[] = {1, 1, 2};
    for (size_t i = 0; i < 3; ++i) {
        input[i].length = strlen(input[i].source);
        source_include_error_t error = {0};
        assert(parse_source_includes(input[i].path,
                                     input[i].source,
                                     input[i].length,
                                     &input[i].includes,
                                     &error));
        input[i].targets = i == 0 ? targets : targets + 2;
    }
    module_sources_t sources = {.files = input, .count = 3};
    module_compilation_t *compiled = NULL;
    module_compilation_error_t error = {0};
    if (!compile_module_sources(&sources, &compiled, &error)) {
        fprintf(stderr, "%s\n", error.message);
        abort();
    }
    void *image = malloc(compiled->code->buffer_size);
    assert(image);
    memcpy(image, compiled->code->buffer, compiled->code->buffer_size);
    process_t *p = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
    assert(prepare_process_launch(p, module_files_identity(files), 0, "/gc/main.goat"));
    static const builtin_function_t descriptors[] = {{.name = U"awaitGate", .execute = await_gate},
                                                     {.name = U"probe", .execute = probe}};
    builtin_function_object_t storage[2] = {0};
    object_static_string_t keys[2] = {0};
    for (size_t i = 0; i < 2; ++i)
        assert(create_object_property(module_file_scope(module_files_get(files, i + 1)),
                                      get_static_string_object(&keys[i], descriptors[i].name),
                                      get_builtin_function_object(&storage[i], &descriptors[i]),
                                      true)
               == MSTAT_OK);
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    process_execution_enter(p);
    assert(configure_gc_boundaries(p, true, true));
    if (!recursive)
        process_execution_leave(p);
    int status = run_module_file(p, module_files_get(files, entry ? 1 : 0));
    if (status && !(entry && failed) && p->main_thread->exception.value) {
        string_value_t message = convert_object_to_string(p->main_thread->exception.value);
        char *text = encode_utf8(message.data);
        fprintf(stderr, "failure: %s\n", text);
        FREE(text);
        FREE_STRING(message);
    }
    assert((status != 0) == (entry && failed));
    if (recursive)
        process_execution_leave(p);
    assert(observations == 1);
    object_static_string_t result_key = {0};
    object_t *result =
        get_object_property(module_file_scope(module_files_get(files, entry ? 1 : 0)),
                            get_static_string_object(&result_key, U"result"));
    if (!(entry && failed))
        assert(result && get_object_integer_value(result).value == (entry ? 42 : 84));
    if (failed) {
        exception_t first = module_file_exception(module_files_get(files, 1));
        exception_t leaf = module_file_exception(module_files_get(files, 2));
        assert(first.value && first.value == leaf.value && first.trace == leaf.trace);
    }
    assert(module_file_state(module_files_get(files, 1))
           == (failed ? MODULE_FILE_FAILED : MODULE_FILE_INITIALIZED));
    assert(!p->initializing_file && !p->initialization_count && !p->initialization_waiters);
    assert(!p->gc_session && !p->gc_roots_size);
    thread_t *t = p->main_thread;
    do {
        assert(!t->initialization_depth && !t->gc_initialization_depth);
        assert(!t->gc_call_participant && !t->gc_join_ready);
        t = t->next;
    } while (t != p->main_thread);
    assert(!memcmp(image, compiled->code->buffer, compiled->code->buffer_size));
    free(image);
    destroy_process(p);
    free_module_compilation(compiled);
    clear_module_compilation_error(&error);
    for (size_t i = 0; i < 3; ++i)
        clear_source_includes(&input[i].includes);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
