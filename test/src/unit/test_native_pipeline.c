/** @file test_native_pipeline.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises analysis through generated adapters, including bytecode retry.
 */
#include "test_native_pipeline.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/launcher.h"
#include "codegen/linker.h"
#include "codegen/native_continuation.h"
#include "codegen/native_pipeline.h"
#include "functional/native_abi/test_allocator.h"
#include "graph/expression.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "lib/threads/os_thread.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "native_test_thread.h"
#include "test_macro.h"
#include "test_output.h"
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

/** @brief Native calls, VM fallback, failures and thread execution must not patch the image. */
static int run_immutable(process_t *process, bytecode_t *code) {
    void *snapshot = ALLOC(code->buffer_size);
    memcpy(snapshot, code->buffer, code->buffer_size);
    int status = run(process, code);
    bool unchanged = !memcmp(snapshot, code->buffer, code->buffer_size);
    FREE(snapshot);
    return unchanged ? status : -1;
}

static const char *compiler;
static const char *invalid_compiler;
static const char *wrong_compiler;
static const char *program_path;
static native_prepare_status_t preparation;
static size_t omitted;

/** @brief Replacement children can share functions; emit each deferred body once. */
static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        instr_index_t index = get_function_bytecode_instruction(node);
        if (index != BAD_INSTR_INDEX
            && ((instruction_t *)code->instructions)[index - 1].arg1 == UINT32_MAX)
            generate_deferred_bytecode_from_node(node, code, data);
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        deferred(get_node_child(node, i), code, data);
}

/** @brief Destroys the source graph before running any generated code. */
static bytecode_t *compile(const gchar_t *source, const char *command) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    options_t *options = create_options();
    bytecode_t *result = NULL;
    if (root && !analyze(root, &memory, options, NULL)) {
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        deferred(root, code, data);
        result = link_code_and_data(code, data);
        void *snapshot = ALLOC(result->buffer_size);
        memcpy(snapshot, result->buffer, result->buffer_size);
        native_prepare_result_t prepared = prepare_native_execution(root, result, command);
        if (memcmp(snapshot, result->buffer, result->buffer_size))
            prepared.status = NATIVE_PREPARE_BIND_ERROR;
        FREE(snapshot);
        preparation = prepared.status;
        omitted = prepared.omitted_specializations;
        if (command == compiler && preparation != NATIVE_PREPARE_READY && prepared.diagnostic)
            FPRINTF(stderr, "%s\n", prepared.diagnostic);
        destroy_native_prepare_result(&prepared);
        destroy_code_builder(code);
        destroy_data_builder(data);
    }
    destroy_options(options);
    destroy_arena(arena);
    return result;
}

static object_t *variable(process_t *proc, const gchar_t *name) {
    object_t *key = create_string_object(proc, (string_value_t){name, STRLEN(name), false});
    object_t *value = get_object_property(proc->main_thread->context->data, key);
    DECREF(key);
    return value;
}

static bool integer(process_t *proc, const gchar_t *name, int64_t value) {
    object_t *object = variable(proc, name);
    return object && is_integer_object(object) && get_object_integer_value(object).value == value;
}

static bool counters(process_t *proc, size_t attempts, size_t successes, size_t retries) {
    thread_t *thread = proc->main_thread;
    return thread->native_attempts == attempts && thread->native_successes == successes
           && thread->native_retries == retries && !thread->native_status
           && !thread->context->native_disabled && !thread->data_stack->size;
}

/* Deferred readers keep the storage inspected by the host observable to analysis. */
static bool dispatch(void) {
    bytecode_t *code = compile(U"const f=func(n){return n;};const g=func(a,b){return a-b;};"
                               U"const z=func(){return 42;};var a=f(7);var b=f(2.5);"
                               U"var c=g(8,0.5);var d=z();func(){a;b;c;d;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    /* A missing native binding must not silently pass through the original body. */
    for (size_t i = 0; i < code->instructions_count; i++)
        if (((instruction_t *)code->instructions)[i].opcode == FUNC)
            ((instruction_t *)
                 code->instructions)[((instruction_t *)code->instructions)[i - 1].arg1] =
                (instruction_t){.opcode = END};
    process_t *proc = create_process();
    ASSERT(!run_immutable(proc, code));
    ASSERT(integer(proc, U"a", 7) && integer(proc, U"d", 42));
    ASSERT(variable(proc, U"b") && is_real_object(variable(proc, U"b"))
           && get_object_real_value(variable(proc, U"b")).value == 2.5);
    ASSERT(variable(proc, U"c") && get_object_real_value(variable(proc, U"c")).value == 7.5);
    ASSERT(counters(proc, 4, 4, 0));
    free_bytecode(code);
    object_t *function = variable(proc, U"f");
    push_object_onto_stack(proc->main_thread->data_stack, create_integer_object(proc, 9));
    ASSERT(call_object(function, 1, NULL, proc->main_thread));
    object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(is_integer_object(result) && get_object_integer_value(result).value == 9);
    DECREF(result);
    ASSERT(counters(proc, 5, 5, 0));
    destroy_process(proc);
    return true;
}

/* A direct call supplies the scalar signature without relying on dynamic-call analysis.
 * Verify and exclude that warmup before measuring only the mixed operator path. */
static bool mixed_baseline;

static operation_result_t
mixed_reset_counters(object_t **arguments, uint16_t count, thread_t *thread) {
    (void)arguments;
    mixed_baseline = count == 0 && thread->native_attempts == 1 && thread->native_successes == 1
                     && thread->native_retries == 0 && !thread->native_status;
    thread->native_attempts = thread->native_successes = thread->native_retries = 0;
    return operation_success(get_null_object());
}

/** @brief Real emitted caller -> VM-only operator -> emitted scalar helper -> caller. */
static bool generated_operator_bridge(void) {
    bytecode_t *code = compile(
        U"const caller=func(a,b){if(arguments.length!=3||arguments[2]!=99)throw 1;return a+b;};"
        U"const helper=func(n){return n+1;};"
        U"var receiver={};"
        U"const handler=func(b){if(this!=receiver||arguments.length!=1||b!=40)throw 2;"
        U"var answer={};answer.value=helper(40);return answer;};"
        U"helper(0);resetNativeCounters();"
        U"receiver[\"+\"]=handler;var result=caller(receiver,40,99);"
        U"if(result.value!=41)throw 3;func(){result;receiver;};",
        compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY && code->function_count == 4);
    const bytecode_function_t *caller = &code->functions[0];
    const bytecode_function_t *helper = &code->functions[1];
    const bytecode_function_t *handler = &code->functions[2];
    native_function_descriptor_t *caller_descriptor =
        get_bytecode_native_function(code, caller->instruction);
    native_function_descriptor_t *helper_descriptor =
        get_bytecode_native_function(code, helper->instruction);
    ASSERT(caller_descriptor && get_native_function_continuation(caller_descriptor));
    ASSERT(helper_descriptor && get_native_function_entry_count(helper_descriptor) > 0);
    /* The middle edge must use the VM regardless of optional generator improvements. */
    ASSERT(bind_bytecode_native_function(code, handler->instruction, NULL));
    ASSERT(!get_bytecode_native_function(code, handler->instruction));
    process_t *proc = create_process();
    static builtin_function_object_t reset_storage;
    static const builtin_function_t reset_descriptor = {.name = U"resetNativeCounters",
                                                        .execute = mixed_reset_counters};
    object_t *key = create_string_object(proc, STATIC_STRING(U"resetNativeCounters"));
    ASSERT(create_object_property(proc->main_thread->context->data,
                                  key,
                                  get_builtin_function_object(&reset_storage, &reset_descriptor),
                                  true)
           == MSTAT_OK);
    DECREF(key);
    mixed_baseline = false;
    ASSERT(!run_immutable(proc, code) && mixed_baseline);
    ASSERT(variable(proc, U"result") && variable(proc, U"result") != variable(proc, U"receiver"));
    ASSERT(counters(proc, 2, 2, 0));
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

static const gchar_t *const recursion_source =
    U"const f=func(n){if(n<=0)return 0;return f(n-1)+1;};"
    U"var hits=199;var extras=0;"
    U"var value=f(++hits,++extras);var small=f(3);func(){value;small;hits;extras;};";

static bool recursion(void) {
    bytecode_t *code = compile(recursion_source, compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run_immutable(proc, code));
    ASSERT(integer(proc, U"value", 200) && integer(proc, U"small", 3) && integer(proc, U"hits", 200)
           && integer(proc, U"extras", 1));
    ASSERT(counters(proc, 2, 1, 1));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool mutual(void) {
    bytecode_t *code = compile(U"const a=func(n){if(n<=0)return 0;return b(n-1)+1;};"
                               U"const b=func(n){if(n<=0)return 0;return a(n-1)+1;};"
                               U"var value=a(150);var small=b(4);func(){value;small;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run_immutable(proc, code));
    ASSERT(integer(proc, U"value", 150) && integer(proc, U"small", 4));
    ASSERT(counters(proc, 2, 1, 1));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool shallow_calls(void) {
    bytecode_t *code = compile(U"const f=func(n){if(n<=0)return 1;return f(n-1)+f(n-1);};"
                               U"var value=f(12);var small=f(2);func(){value;small;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run_immutable(proc, code));
    ASSERT(integer(proc, U"value", 4096) && integer(proc, U"small", 4));
    ASSERT(counters(proc, 2, 2, 0));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static const goat_native_entry_v2_t *recursive_entry(bytecode_t *code) {
    for (size_t i = 0; i < code->instructions_count; i++) {
        const native_function_descriptor_t *descriptor = get_bytecode_native_function(code, i);
        if (descriptor)
            return get_native_function_entry(descriptor, 0);
    }
    return NULL;
}

typedef struct {
    const goat_native_entry_v2_t *entry;
    bool success;
} adapter_task_t;

static void *adapter_worker(void *data) {
    adapter_task_t *task = data;
    task->success = true;
    for (size_t i = 0; i < 100; i++) {
        goat_native_value_v2_t argument = {.type = GOAT_NATIVE_I64, .value.integer = 200};
        goat_native_value_v2_t result = {.type = 99, .reserved = 77, .value.integer = -123};
        goat_native_value_v2_t original = result;
        task->success &=
            test_invoke(task->entry->invoke, GOAT_NATIVE_ABI_VERSION, 1, &argument, &result)
            == GOAT_NATIVE_RESOURCE_LIMIT;
        task->success &= !memcmp(&original, &result, sizeof(result));
        argument.value.integer = 3;
        task->success &=
            test_invoke(task->entry->invoke, GOAT_NATIVE_ABI_VERSION, 1, &argument, &result)
            == GOAT_NATIVE_OK;
        task->success &=
            result.type == GOAT_NATIVE_I64 && !result.reserved && result.value.integer == 3;
    }
    return NULL;
}

static bool adapter_guards(void) {
    bytecode_t *code =
        compile(U"const f=func(n){if(n<=0)return 0;return f(n-1)+1;};var x=f(3);", compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    const goat_native_entry_v2_t *entry = recursive_entry(code);
    ASSERT(entry);
    adapter_task_t tasks[4];
    os_thread_t *threads[4];
    for (size_t i = 0; i < 4; i++) {
        tasks[i] = (adapter_task_t){.entry = entry};
        ASSERT(create_os_thread(&threads[i], adapter_worker, &tasks[i], 0) == OS_THREAD_OK);
    }
    for (size_t i = 0; i < 4; i++) {
        ASSERT(join_os_thread(threads[i], NULL) == OS_THREAD_OK);
        ASSERT(destroy_os_thread(threads[i]) == OS_THREAD_OK);
        ASSERT(tasks[i].success);
    }
    free_bytecode(code);
    return true;
}

typedef struct {
    bytecode_t *code;
    process_t *proc;
    bool success;
} stack_task_t;

static void *small_stack_worker(void *data) {
    stack_task_t *task = data;
    task->success = !native_stack_has_headroom() && !run_immutable(task->proc, task->code)
                    && integer(task->proc, U"x", 7) && counters(task->proc, 0, 0, 1);
    return NULL;
}

static bool small_stack(void) {
    bytecode_t *code = compile(U"const f=func(n){return n;};var x=f(7);func(){x;};", compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    stack_task_t task = {.code = code, .proc = create_process()};
    ASSERT(run_on_small_stack(small_stack_worker, &task));
    ASSERT(task.success);
    destroy_process(task.proc);
    free_bytecode(code);
    return true;
}

static bool failures(void) {
    const char *commands[] = {"goat-missing-compiler-7f01", invalid_compiler, wrong_compiler};
    const native_prepare_status_t statuses[] = {NATIVE_PREPARE_COMPILE_ERROR,
                                                NATIVE_PREPARE_LOAD_ERROR,
                                                NATIVE_PREPARE_BIND_ERROR};
    for (size_t i = 0; i < 3; i++) {
        bytecode_t *code =
            compile(U"const f=func(n){return n+1;};var x=f(7);func(){x;};", commands[i]);
        ASSERT(code && preparation == statuses[i] && !code->native_functions);
        process_t *proc = create_process();
        ASSERT(!run_immutable(proc, code) && integer(proc, U"x", 8) && counters(proc, 0, 0, 0));
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

static bool empty(void) {
    bytecode_t *code = compile(U"var x=7;", "goat-missing-compiler-7f01");
    ASSERT(code && preparation == NATIVE_PREPARE_EMPTY && !code->native_functions);
    free_bytecode(code);
    return true;
}

static bool large_frame(void) {
    string_builder_t source;
    init_string_builder(&source, 512);
    append_string(&source, U"const f=func(n){var x=n;");
    for (size_t i = 0; i < 80; i++)
        append_string(&source, U"x=x+n;");
    string_value_t text = append_string(&source, U"return x;};var value=f(1);func(){value;};");
    bytecode_t *code = compile(text.data, "goat-missing-compiler-7f01");
    FREE_STRING(text);
    ASSERT(code && preparation == NATIVE_PREPARE_EMPTY && omitted && !code->native_functions);
    process_t *proc = create_process();
    ASSERT(!run_immutable(proc, code) && integer(proc, U"value", 81));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool launcher(void) {
    options_t *options = create_options();
    options->input_file = create_path(program_path);
    options->native_execution = NATIVE_REQUIRED;
    int status = go(options);
    destroy_options(options);
    ASSERT(!status);
    return true;
}

/** @brief Deferred child operators cannot accidentally select their enclosing function. */
static bool continuation_emission(void) {
    const instruction_t instructions[] = {{.opcode = ARG, .arg1 = 6},
                                          {.opcode = FUNC},
                                          {.opcode = ARG, .arg1 = 8},
                                          {.opcode = FUNC},
                                          {.opcode = ARG, .arg1 = 11},
                                          {.opcode = FUNC},
                                          {.opcode = NIL},
                                          {.opcode = RET},
                                          {.opcode = GETINDEX},
                                          {.opcode = SETINDEX},
                                          {.opcode = RET},
                                          {.opcode = LAND},
                                          {.opcode = CALLM},
                                          {.opcode = BOOL},
                                          {.opcode = RET}};
    const bytecode_function_t functions[] = {{.id = 1, .instruction = 1, .body = 6},
                                             {.id = 2, .instruction = 3, .body = 8},
                                             {.id = 3, .instruction = 5, .body = 11}};
    bytecode_t code = {.instructions = instructions,
                       .instructions_count = sizeof(instructions) / sizeof(*instructions),
                       .functions = functions,
                       .function_count = 3};
    instruction_t snapshot[sizeof(instructions) / sizeof(*instructions)];
    memcpy(snapshot, instructions, sizeof(snapshot));
    native_continuation_output_t output = generate_native_continuations(NULL, &code, NULL, NULL);
    ASSERT(output.count == 2 && output.source.data);
    ASSERT(output.entries[0].function_id == 2 && output.entries[0].function_instruction == 3
           && output.entries[0].first_instruction == 8 && output.entries[0].instruction_count == 3);
    ASSERT(output.entries[1].function_id == 3 && output.entries[1].first_instruction == 11
           && output.entries[1].instruction_count == 4);
    ASSERT(STRSTR(output.source.data, U"switch (pc)")
           && STRSTR(output.source.data, U"context->step(context->state, 8, &next)")
           && STRSTR(output.source.data, U"context->step(context->state, 14, &next)")
           && !STRSTR(output.source.data, U"case 6:"));
    ASSERT(STRSTR(output.source.data, U"if (status != GOAT_NATIVE_CONTINUE) return status;")
           && STRSTR(output.source.data, U"goat_native_continuation_query"));
    ASSERT(!memcmp(snapshot, instructions, sizeof(snapshot)));
    destroy_native_continuation_output(&output);
    ASSERT(!output.source.data && !output.count && !output.entries);
    /* A later file's top-level operators are not part of the previous file's last body. */
    const instruction_t split_instructions[] = {{.opcode = ARG, .arg1 = 3},
                                                {.opcode = FUNC},
                                                {.opcode = END},
                                                {.opcode = ADD},
                                                {.opcode = RET},
                                                {.opcode = CALL},
                                                {.opcode = ARG, .arg1 = 9},
                                                {.opcode = FUNC},
                                                {.opcode = END},
                                                {.opcode = SUB},
                                                {.opcode = RET}};
    const bytecode_file_t files[] = {{0, 5, 0, 0}, {5, 6, 5, 0}};
    const bytecode_function_t split_functions[] = {
        {.id = 1, .instruction = 1, .body = 3, .file = 0},
        {.id = 2, .instruction = 7, .body = 9, .file = 1}};
    code = (bytecode_t){.instructions = split_instructions,
                        .instructions_count = 11,
                        .files = files,
                        .file_count = 2,
                        .functions = split_functions,
                        .function_count = 2};
    output = generate_native_continuations(NULL, &code, NULL, NULL);
    ASSERT(output.count == 2 && output.entries[0].instruction_count == 2
           && output.entries[1].instruction_count == 2);
    ASSERT(!STRSTR(output.source.data, U"case 5:") && !STRSTR(output.source.data, U"case 8:"));
    destroy_native_continuation_output(&output);
    const uint8_t families[] = {UPLUS, UMINUS, INC,    DEC,   LNOT,     BOOL,     BNOT,      LAND,
                                LOR,   BAND,   BOR,    BXOR,  SHL,      SHR,      ADD,       SUB,
                                MUL,   DIVIDE, MODULO, POWER, LESS,     LEQ,      GREATER,   GREQ,
                                EQUAL, DIFF,   CALL,   CALLM, GETINDEX, SETINDEX, CHECKINDEX};
    instruction_t body[] = {{.opcode = ADD}, {.opcode = RET}};
    const bytecode_function_t single = {.id = 1, .body = 0};
    code = (bytecode_t){.instructions = body,
                        .instructions_count = 2,
                        .functions = &single,
                        .function_count = 1};
    for (size_t i = 0; i < sizeof(families) / sizeof(*families); ++i) {
        body[0].opcode = families[i];
        output = generate_native_continuations(NULL, &code, NULL, NULL);
        ASSERT(output.count == 1 && output.entries[0].instruction_count == 2);
        destroy_native_continuation_output(&output);
    }
    return true;
}

typedef struct {
    unsigned calls;
    uint32_t terminal, next;
    bool ordered;
} continuation_probe_t;

static uint32_t GOAT_NATIVE_CALL continuation_probe(void *opaque, uint32_t pc, uint32_t *next) {
    continuation_probe_t *probe = opaque;
    probe->ordered &= pc == (probe->calls ? 4u : 2u);
    ++probe->calls;
    if (probe->terminal != GOAT_NATIVE_CONTINUE)
        return probe->terminal;
    if (pc == 4)
        return GOAT_NATIVE_COMPLETE;
    *next = probe->next;
    return GOAT_NATIVE_CONTINUE;
}

/** @brief Execute the emitted machine code, including branching and terminal statuses. */
static bool continuation_protocol(void) {
    const instruction_t instructions[] = {{.opcode = ARG, .arg1 = 2},
                                          {.opcode = FUNC},
                                          {.opcode = ADD},
                                          {.opcode = NIL},
                                          {.opcode = RET}};
    const bytecode_function_t function = {.id = 1, .instruction = 1, .body = 2};
    bytecode_t code = {.instructions = instructions,
                       .instructions_count = 5,
                       .functions = &function,
                       .function_count = 1};
    arena_t *arena = create_arena(32);
    c_module_t module = {0};
    c_module_output_t scalar = generate_c_module(arena, &module);
    native_continuation_output_t output =
        generate_native_continuations(NULL, &code, &module, &scalar);
    ASSERT(output.count == 1);
    source_builder_t *builder = create_source_builder();
    add_formatted_source(builder, 0, scalar.source);
    add_formatted_source(builder, 0, output.source);
    output.source = EMPTY_STRING_VALUE;
    string_value_t source = build_source(builder);
    destroy_source_builder(builder);
    native_workspace_t *workspace = create_native_workspace();
    ASSERT(workspace);
    native_compile_result_t compiled =
        compile_native_library(source.data, compiler, workspace->library);
    FREE_STRING(source);
    ASSERT(compiled.status == NATIVE_COMPILE_OK);
    destroy_native_compile_result(&compiled);
    native_library_result_t loaded = load_native_library(workspace->library);
    ASSERT(loaded.status == NATIVE_LIBRARY_OK);
    ASSERT(get_native_library_entry_count(loaded.library) == 0
           && get_native_library_continuation_count(loaded.library) == 1);
    const goat_native_continuation_entry_t *entry =
        get_native_library_continuation(loaded.library, 0);
    continuation_probe_t probe = {.ordered = true, .next = 4};
    goat_native_continuation_context_t context = {.state = &probe, .step = continuation_probe};
    ASSERT(entry->invoke(&context, 2) == GOAT_NATIVE_COMPLETE && probe.calls == 2 && probe.ordered);
    for (uint32_t terminal = GOAT_NATIVE_COMPLETE; terminal <= GOAT_NATIVE_FAILED; ++terminal) {
        probe = (continuation_probe_t){.ordered = true, .terminal = terminal};
        ASSERT(entry->invoke(&context, 2) == terminal && probe.calls == 1 && probe.ordered);
    }
    probe = (continuation_probe_t){.ordered = true, .next = 99};
    ASSERT(entry->invoke(&context, 2) == GOAT_NATIVE_FAILED && probe.calls == 1);
    probe.calls = 0;
    ASSERT(entry->invoke(&context, 1) == GOAT_NATIVE_FAILED && !probe.calls);
    ASSERT(entry->invoke(&context, 5) == GOAT_NATIVE_FAILED && !probe.calls);
    ASSERT(entry->invoke(NULL, 2) == GOAT_NATIVE_FAILED);
    context.step = NULL;
    ASSERT(entry->invoke(&context, 2) == GOAT_NATIVE_FAILED && !probe.calls);
    destroy_native_library_result(&loaded);
    destroy_native_workspace(workspace);
    destroy_native_continuation_output(&output);
    destroy_arena(arena);
    return true;
}

bool test_native_pipeline(const char *command,
                          const char *invalid,
                          const char *wrong,
                          const char *program) {
    compiler = command;
    invalid_compiler = invalid;
    wrong_compiler = wrong;
    program_path = program;
    test_output_start("native pipeline");

    static const struct {
        const char *name;
        bool (*test)(void);
    } tests[] = {
        {"compiled operator continuation inventory and immutable PCs", continuation_emission},
        {"generated continuation branching, terminal statuses and invalid PCs",
         continuation_protocol},
        {"generated numeric dispatch after graph destruction", dispatch},
        {"generated caller, VM operator, generated helper and arbitrary result",
         generated_operator_bridge},
        {"depth retry preserves arguments and disables descendant native calls", recursion},
        {"mutual recursion retries once", mutual},
        {"many shallow calls remain native without retry", shallow_calls},
        {"adapter guards reset, preserve output and isolate threads", adapter_guards},
        {"small thread stack falls back before adapter entry", small_stack},
        {"compile, load and metadata failures preserve bytecode", failures},
        {"empty inventory never invokes compiler", empty},
        {"oversized scalar frame remains bytecode", large_frame},
        {"launcher integrates preparation and execution", launcher}};

    size_t passed = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
        size_t memory = get_allocated_memory_size();
        bool ok = tests[i].test();
        if (get_allocated_memory_size() != memory) {
            FPRINTF(stderr, "Allocation balance changed in %s\n", tests[i].name);
            ok = false;
        }
        test_output_case(ok, "%s", tests[i].name);
        passed += ok;
    }
    size_t failed = sizeof(tests) / sizeof(*tests) - passed;
    test_output_summary("Native pipeline", passed, failed);
    return !failed;
}
