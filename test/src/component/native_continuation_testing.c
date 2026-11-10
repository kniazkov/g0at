/** @file native_continuation_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native PC driver boundaries, arbitrary objects and non-replaying VM transitions.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/process.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t entries, steps, effects;
static bool fail_after_effect, invalid_pc, escaping_exception, language_failure;
static bool host_cancellation, backend_failure;

static uint32_t GOAT_NATIVE_CALL invoke(const goat_native_continuation_context_t *context,
                                        uint32_t pc) {
    ++entries;
    if (invalid_pc) {
        uint32_t next;
        ++steps;
        return context->step(context->state, UINT32_MAX, &next);
    }
    for (;;) {
        uint32_t next;
        ++steps;
        uint32_t status = context->step(context->state, pc, &next);
        if (status != GOAT_NATIVE_CONTINUE)
            return status;
        if (fail_after_effect && effects)
            return GOAT_NATIVE_FAILED;
        pc = next;
    }
}

static operation_result_t mark(object_t **arguments, uint16_t count, thread_t *thread) {
    (void)arguments;
    assert(!count);
    ++effects;
    collect_garbage(thread->process);
    if (host_cancellation) {
        thread->process->exit_requested = true;
        thread->process->exit_code = 7;
        thread->process->native_failed = backend_failure;
    }
    return operation_success(get_null_object());
}

static void setup(process_t *process) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"mark", .execute = mark};
    object_t *name = create_string_object(process, STATIC_STRING(U"mark"));
    assert(create_object_property(process->main_thread->context->data,
                                  name,
                                  get_builtin_function_object(&storage, &descriptor),
                                  true)
           == MSTAT_OK);
    DECREF(name);
}

static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, code, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), code, data);
}

static void program(const gchar_t *source,
                    unsigned native_functions,
                    size_t expected_entries,
                    size_t expected_effects,
                    bool failure) {
    size_t before = get_allocated_memory_size();
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node =
        parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    assert(node);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, builder, data);
    deferred(node, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    assert(code && validate_bytecode_image(code));
    assert(code->function_count >= native_functions);
    void *image = malloc(code->buffer_size);
    assert(image);
    memcpy(image, code->buffer, code->buffer_size);
    for (unsigned i = 0; i < native_functions; ++i) {
        const bytecode_function_t *function = &code->functions[i];
        uint32_t end = (uint32_t)code->instructions_count;
        for (size_t j = 0; j < code->function_count; ++j)
            if (code->functions[j].body > function->body && code->functions[j].body < end)
                end = code->functions[j].body;
        goat_native_continuation_entry_t entry = {.function_id = function->id,
                                                  .function_instruction = function->instruction,
                                                  .first_instruction = function->body,
                                                  .instruction_count = end - function->body,
                                                  .invoke = invoke};
        /* A syntactically valid descriptor may not shorten or extend the actual body. */
        goat_native_continuation_entry_t invalid = entry;
        ++invalid.instruction_count;
        native_function_descriptor_t *descriptor =
            create_native_continuation_descriptor(&invalid, NULL, NULL);
        assert(descriptor);
        assert(!bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
        invalid = entry;
        assert(invalid.instruction_count > 1);
        --invalid.instruction_count;
        descriptor = create_native_continuation_descriptor(&invalid, NULL, NULL);
        assert(descriptor);
        assert(!bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
        descriptor = create_native_continuation_descriptor(&entry, NULL, NULL);
        assert(descriptor);
        assert(bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
    }
    process_t *process = create_process();
    setup(process);
    thread_t *thread = process->main_thread;
    context_t *caller = thread->context;
    entries = steps = effects = 0;
    int status = run(process, code);
    if (status && !failure)
        fprintf(stderr,
                "Unexpected continuation failure, native status %u\n",
                thread->native_status);
    assert((status != 0) == failure);
    if (host_cancellation)
        assert(status == (backend_failure ? 1 : 7));
    assert(entries == expected_entries && effects == expected_effects && steps >= entries);
    assert(thread->native_attempts == entries && thread->native_retries == 0);
    assert(thread->native_successes == (failure || escaping_exception ? 0 : entries));
    assert(thread->data_stack->size == 0);
    if (language_failure) {
        assert(thread->context == get_root_context());
        assert(thread->exception.value == get_static_integer_object(73));
        assert(thread->native_status == GOAT_NATIVE_OK);
        assert(exception_trace_size(thread->exception.trace) >= 2);
        const exception_trace_frame_t *origin = exception_trace_frame(thread->exception.trace, 0);
        assert(origin->instruction < code->instructions_count);
        assert(code->instructions[origin->instruction].opcode == THROW);
    } else {
        assert(thread->context == caller && !thread->exception.value);
        if (host_cancellation && !backend_failure) {
            assert(!process->native_failed && thread->native_status == GOAT_NATIVE_OK);
            assert(!thread->exception.trace);
        } else if (failure)
            assert(thread->native_status == GOAT_NATIVE_BAD_REQUEST);
    }
    if (fail_after_effect)
        assert(steps == 2);
    assert(!memcmp(image, code->buffer, code->buffer_size));
    free(image);
    destroy_process(process);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    assert(get_allocated_memory_size() == before);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "mixed")) {
        /* Only caller and helper get drivers: the intervening operator handler is VM-only. */
        program(
            U"var caller=func(a,b){if(arguments.length!=3 || arguments[2]!=b)throw 3;return a+b;};"
            U"var helper=func(x){mark();return x;};"
            U"var a={};var value={};a[\"+\"]=func(b){"
            U"if(this!=a || arguments.length!=1 || arguments[0]!=b)throw 1;"
            U"return helper(b);};if(caller(a,value,value)!=value)throw 2;",
            2,
            2,
            1,
            false);
    } else if (!strcmp(argv[1], "unwind")) {
        program(U"var caller=func(a){try{return a+1;}catch(e){"
                U"if(e!=73)throw 1;return saved;}finally{mark();}};"
                U"var a={};var saved={};a[\"+\"]=func(x){throw 73;};"
                U"if(caller(a)!=saved)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(argv[1], "escape")) {
        escaping_exception = true;
        /* Escaping throw must return through C before the outer VM catch executes. */
        program(U"var caller=func(a){try{return a+1;}finally{mark();}};"
                U"var a={};a[\"+\"]=func(x){throw 73;};"
                U"var caught=false;try{caller(a);}catch(e){caught=e==73;}"
                U"if(!caught)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(argv[1], "trace")) {
        language_failure = true;
        program(U"var caller=func(a){try{return a+1;}finally{mark();}};"
                U"var a={};a[\"+\"]=func(x){throw 73;};caller(a);",
                1,
                1,
                1,
                true);
    } else if (!strcmp(argv[1], "strict-bool")) {
        escaping_exception = true;
        program(U"var caller=func(a){return a<1;};"
                U"var a={};a[\"<\"]=func(x){mark();return {};};"
                U"var caught=false;try{caller(a);}catch(e){caught=true;}"
                U"if(!caught)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(argv[1], "recursion")) {
        program(U"var caller=func(n){mark();if(n==0)return saved;return caller(n-1);};"
                U"var saved={};if(caller(8)!=saved)throw 2;",
                1,
                9,
                9,
                false);
    } else if (!strcmp(argv[1], "no-replay")) {
        fail_after_effect = true;
        program(U"var caller=func(){mark();return 1;};caller();", 1, 1, 1, true);
    } else if (!strcmp(argv[1], "host-cancel") || !strcmp(argv[1], "cancel-backend")) {
        host_cancellation = true;
        backend_failure = !strcmp(argv[1], "cancel-backend");
        program(U"var caller=func(){mark();return 1;};caller();", 1, 1, 1, true);
        assert(steps == 2);
    } else if (!strcmp(argv[1], "range")) {
        invalid_pc = true;
        program(U"var caller=func(){mark();return 1;};caller();", 1, 1, 0, true);
        assert(steps == 1);
    } else {
        assert(!"unknown case");
    }
    return 0;
}
