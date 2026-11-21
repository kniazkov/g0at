/** @file gc_io_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real pipe I/O while another VM worker collects through production boundaries.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "common/module_compilation.h"
#include "lib/memory/allocate.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/module_files.h"
#include "unit/analysis_test_support.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    define pipe_fds(p) _pipe(p, 4096, _O_BINARY)
#    define dup_fd _dup
#    define dup_to _dup2
#    define close_fd _close
#    define read_fd _read
#    define write_fd _write
#    define stream_fd _fileno
#else
#    include <unistd.h>
#    define pipe_fds pipe
#    define dup_fd dup
#    define dup_to dup2
#    define close_fd close
#    define read_fd read
#    define write_fd write
#    define stream_fd fileno
#endif

static process_t *process;
static bool input_mode, eof_mode, invalid_mode, newline;
static int peer;
static uint64_t parked_collections;
static const size_t payload_size = 131072;
static bool transferred;
static bool startup;

static operation_result_t await_io(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && (startup || thread->program_worker));
    gc_session_t *session = process->gc_session;
    gc_participant_t *main = session->participants;
    while (main && (startup ? !main->thread->program_worker : main->thread != process->main_thread))
        main = main->next;
    /* Startup may not have registered yet. Only a live call, not scheduling,
       proves that the pipe operation has published its managed arguments. */
    while (!main || !main->published || main->thread->gc_call_participant != main) {
        size_t depth = process_execution_suspend(process);
        process_execution_resume(process, depth);
        main = session->participants;
        while (main
               && (startup ? !main->thread->program_worker : main->thread != process->main_thread))
            main = main->next;
    }
    assert(!main->blocked);
    parked_collections = process->gc_statistics->explicit_collections;
    return operation_success(get_null_object());
}

static operation_result_t transfer(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && (startup || thread->program_worker));
    assert(process->gc_statistics->explicit_collections > parked_collections + 10);
    assert(process->gc_statistics->last.after.objects < 100);
    size_t depth = process_execution_suspend(process);
    if (input_mode) {
        if (!eof_mode)
            assert(invalid_mode ? write_fd(peer, "\xff\n", 2) == 2
                                : write_fd(peer, "answer\n", 7) == 7);
    } else {
        char buffer[4096];
        size_t total = payload_size + 1 + (newline ? 1 : 0), offset = 0;
        while (offset < total) {
            int n = (int)read_fd(peer, buffer, sizeof(buffer));
            assert(n > 0 && (size_t)n <= total - offset);
            for (int i = 0; i < n; ++i, ++offset)
                assert(buffer[i]
                       == (offset < payload_size    ? 'x'
                           : offset == payload_size ? '!'
                                                    : '\n'));
        }
    }
    assert(close_fd(peer) == 0);
    process_execution_resume(process, depth);
    transferred = true;
    return operation_success(get_null_object());
}

static void deferred(node_t *node, code_builder_t *builder, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, builder, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), builder, data);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    invalid_mode = !strcmp(argv[1], "invalid");
    input_mode = invalid_mode || !strcmp(argv[1], "input") || !strcmp(argv[1], "eof");
    eof_mode = !strcmp(argv[1], "eof");
    newline = !strcmp(argv[1], "println");
    bool method = !strcmp(argv[2], "method");
    bool initializer = !strcmp(argv[2], "initializer");
    startup = !strcmp(argv[2], "startup");
    size_t baseline = get_allocated_memory_size();
    int pipes[2];
    assert(pipe_fds(pipes) == 0);
    FILE *stream = input_mode ? stdin : stdout;
    int descriptor = stream_fd(stream), saved = dup_fd(descriptor);
    assert(saved >= 0);
    assert(setvbuf(stream, NULL, _IONBF, 0) == 0);
    assert(dup_to(pipes[input_mode ? 0 : 1], descriptor) >= 0);
    assert(close_fd(pipes[input_mode ? 0 : 1]) == 0);
    peer = pipes[input_mode ? 1 : 0];

    process = create_process();
    static const builtin_function_t descriptors[] = {{.name = U"awaitIO", .execute = await_io},
                                                     {.name = U"transfer", .execute = transfer}};
    builtin_function_object_t storage[2] = {0};
    object_static_string_t keys[2] = {0};
    for (size_t i = 0; i < 2; ++i)
        create_object_property(process->main_thread->context->data,
                               get_static_string_object(&keys[i], descriptors[i].name),
                               get_builtin_function_object(&storage[i], &descriptors[i]),
                               true);
    gchar_t *payload = ALLOC((payload_size + 1) * sizeof(*payload));
    for (size_t i = 0; i < payload_size; ++i)
        payload[i] = 'x';
    payload[payload_size] = 0;
    object_t *value = create_string_object(process, (string_value_t){payload, payload_size, true});
    object_static_string_t payload_key = {0};
    create_object_property(process->main_thread->context->data,
                           get_static_string_object(&payload_key, U"payload"),
                           value,
                           true);
    DECREF(value);
    const gchar_t *operation = input_mode ? U"input" : newline ? U"println" : U"print";
    string_value_t source = format_string(
        U"var t=spawn(func(){awaitIO();var i=0;while(i<100){var a=[];a.push(a);i+=1;}transfer();});"
        U"var obj={var io=%s;};%s%s%s%s",
        operation,
        invalid_mode ? U"var caught=false;try{"
        : input_mode ? U"var result="
                     : U"",
        method ? U"obj.io" : operation,
        input_mode ? U"([42]);" : U"(payload+\"!\",[42]);",
        invalid_mode ? U"}catch(e){caught=true;}if(!caught)throw 1;"
        : eof_mode   ? U"if(result!=\"\")throw 1;"
        : input_mode ? U"if(result!=\"answer\")throw 1;"
                     : U"");
    if (startup) {
        FREE_STRING(source);
        source =
            format_string(U"var t=spawn(%s,%s);awaitIO();var i=0;"
                          U"while(i<100){var a=[];a.push(a);i+=1;}transfer();%s",
                          operation,
                          input_mode ? U"[42]" : U"payload+\"!\",[42]",
                          invalid_mode ? U"var caught=false;try{join(t);}catch(e){caught=true;}"
                                         U"if(!caught)throw 1;"
                          : eof_mode   ? U"if(join(t)!=\"\")throw 1;"
                          : input_mode ? U"if(join(t)!=\"answer\")throw 1;"
                                       : U"join(t);");
    }
    bytecode_t *code = NULL;
    module_compilation_t *compiled = NULL;
    module_files_t *files = NULL;
    module_compilation_error_t error = {0};
    if (!initializer) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *node = parse_analysis_test_program(&memory, source);
        assert(node);
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(node, builder, data);
        deferred(node, builder, data);
        code = link_code_and_data(builder, data);
        destroy_code_builder(builder);
        destroy_data_builder(data);
        destroy_arena(arena);
    } else {
        char *text = encode_utf8(source.data);
        char *body = strstr(text, "var obj=");
        assert(body);
        char *main_source =
            "var t=spawn(func(){awaitIO();var i=0;while(i<100){var a=[];a.push(a);i+=1;}"
            "transfer();});include \"io.goat\";";
        module_source_t input[] = {
            {.path = "main.goat",
             .name = "main.goat",
             .source = main_source,
             .length = strlen(main_source)},
            {.path = "io.goat", .name = "io.goat", .source = body, .length = strlen(body)}};
        source_include_error_t syntax = {0};
        size_t target = 1;
        assert(parse_source_includes(input[0].path,
                                     main_source,
                                     input[0].length,
                                     &input[0].includes,
                                     &syntax));
        input[0].targets = &target;
        module_sources_t sources = {.files = input, .count = 2};
        assert(compile_module_sources(&sources, &compiled, &error));
        assert(
            create_module_files_from_snapshot(process, compiled->metadata, compiled->code, &files));
        assert(prepare_process_launch(process, module_files_identity(files), 0, "/gc/main.goat"));
        for (size_t i = 0; i < 2; ++i) {
            object_t *scope = module_file_scope(module_files_get(files, i));
            for (size_t j = 0; j < 2; ++j)
                assert(create_object_property(
                           scope,
                           get_static_string_object(&keys[j], descriptors[j].name),
                           get_builtin_function_object(&storage[j], &descriptors[j]),
                           true)
                       == MSTAT_OK);
            assert(create_object_property(scope,
                                          get_static_string_object(&payload_key, U"payload"),
                                          value,
                                          true)
                   == MSTAT_OK);
        }
        clear_source_includes(&input[0].includes);
        FREE(text);
    }
    FREE_STRING(source);
    gc_stats_t stats;
    assert(configure_gc_statistics(process, &stats));
    process_execution_enter(process);
    assert(configure_gc_boundaries(process, true, true));
    process_execution_leave(process);
    assert((initializer ? run_module_file(process, module_files_main(files)) : run(process, code))
               == 0
           && transferred);
    assert(!process->gc_session && !process->gc_roots_size);
    assert(!process->main_thread->gc_call_participant);
    destroy_process(process);
    if (code)
        free_bytecode(code);
    free_module_compilation(compiled);
    clear_module_compilation_error(&error);
    assert(dup_to(saved, descriptor) >= 0 && close_fd(saved) == 0);
    clearerr(stream);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
