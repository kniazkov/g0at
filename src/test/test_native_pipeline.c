/** @file test_native_pipeline.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises analysis through generated adapters, including bytecode retry.
 */
#include "test_native_pipeline.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/launcher.h"
#include "codegen/linker.h"
#include "codegen/native_pipeline.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "native_test_thread.h"
#include "test_macro.h"
#include "test_output.h"
#include "vm/vm.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

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
        if (index != BAD_INSTR_INDEX && code->instructions[index - 1].arg1 == UINT32_MAX)
            generate_deferred_bytecode_from_node(node, code, data);
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        deferred(get_node_child(node, i), code, data);
}

/** @brief Destroys the source graph before running any generated code. */
static bytecode_t *compile(const wchar_t *source, const char *command) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
    options_t *options = create_options();
    bytecode_t *result = NULL;
    if (root && !analyze(root, &memory, options, NULL)) {
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        deferred(root, code, data);
        result = link_code_and_data(code, data);
        native_prepare_result_t prepared = prepare_native_execution(root, result, command);
        preparation = prepared.status;
        omitted = prepared.omitted_specializations;
        if (command == compiler && preparation != NATIVE_PREPARE_READY && prepared.diagnostic)
            fprintf(stderr, "%s\n", prepared.diagnostic);
        destroy_native_prepare_result(&prepared);
        destroy_code_builder(code);
        destroy_data_builder(data);
    }
    destroy_options(options);
    destroy_arena(arena);
    return result;
}

static object_t *variable(process_t *proc, const wchar_t *name) {
    object_t *key = create_string_object(proc, (string_value_t){name, wcslen(name), false});
    object_t *value = get_object_property(proc->main_thread->context->data, key);
    DECREF(key);
    return value;
}

static bool integer(process_t *proc, const wchar_t *name, int64_t value) {
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
    bytecode_t *code = compile(L"const f=func(n){return n;};const g=func(a,b){return a-b;};"
                               L"const z=func(){return 42;};var a=f(7);var b=f(2.5);"
                               L"var c=g(8,0.5);var d=z();func(){a;b;c;d;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    /* A missing native binding must not silently pass through the original body. */
    for (size_t i = 0; i < code->instructions_count; i++)
        if (code->instructions[i].opcode == FUNC)
            code->instructions[code->instructions[i - 1].arg1] = (instruction_t){.opcode = END};
    process_t *proc = create_process();
    ASSERT(!run(proc, code));
    ASSERT(integer(proc, L"a", 7) && integer(proc, L"d", 42));
    ASSERT(variable(proc, L"b") && is_real_object(variable(proc, L"b"))
           && get_object_real_value(variable(proc, L"b")).value == 2.5);
    ASSERT(variable(proc, L"c") && get_object_real_value(variable(proc, L"c")).value == 7.5);
    ASSERT(counters(proc, 4, 4, 0));
    free_bytecode(code);
    object_t *function = variable(proc, L"f");
    push_object_onto_stack(proc->main_thread->data_stack, create_integer_object(proc, 9));
    ASSERT(call_object(function, 1, proc->main_thread));
    object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(is_integer_object(result) && get_object_integer_value(result).value == 9);
    DECREF(result);
    ASSERT(counters(proc, 5, 5, 0));
    destroy_process(proc);
    return true;
}

static const wchar_t *const recursion_source =
    L"const f=func(n){if(n<=0)return 0;return f(n-1)+1;};"
    L"var hits=199;var extras=0;"
    L"var value=f(++hits,++extras);var small=f(3);func(){value;small;hits;extras;};";

static bool recursion(void) {
    bytecode_t *code = compile(recursion_source, compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run(proc, code));
    ASSERT(integer(proc, L"value", 200) && integer(proc, L"small", 3) && integer(proc, L"hits", 200)
           && integer(proc, L"extras", 1));
    ASSERT(counters(proc, 2, 1, 1));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool mutual(void) {
    bytecode_t *code = compile(L"const a=func(n){if(n<=0)return 0;return b(n-1)+1;};"
                               L"const b=func(n){if(n<=0)return 0;return a(n-1)+1;};"
                               L"var value=a(150);var small=b(4);func(){value;small;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run(proc, code));
    ASSERT(integer(proc, L"value", 150) && integer(proc, L"small", 4));
    ASSERT(counters(proc, 2, 1, 1));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool shallow_calls(void) {
    bytecode_t *code = compile(L"const f=func(n){if(n<=0)return 1;return f(n-1)+f(n-1);};"
                               L"var value=f(12);var small=f(2);func(){value;small;};",
                               compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    process_t *proc = create_process();
    ASSERT(!run(proc, code));
    ASSERT(integer(proc, L"value", 4096) && integer(proc, L"small", 4));
    ASSERT(counters(proc, 2, 2, 0));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static const goat_native_entry_v1_t *recursive_entry(bytecode_t *code) {
    for (size_t i = 0; i < code->instructions_count; i++) {
        const native_function_descriptor_t *descriptor = get_bytecode_native_function(code, i);
        if (descriptor)
            return get_native_function_entry(descriptor, 0);
    }
    return NULL;
}

typedef struct {
    const goat_native_entry_v1_t *entry;
    bool success;
} adapter_task_t;

static void *adapter_worker(void *data) {
    adapter_task_t *task = data;
    task->success = true;
    for (size_t i = 0; i < 100; i++) {
        goat_native_value_v1_t argument = {.type = GOAT_NATIVE_I64, .value.integer = 200};
        goat_native_value_v1_t result = {.type = 99, .reserved = 77, .value.integer = -123};
        goat_native_value_v1_t original = result;
        task->success &=
            task->entry->invoke(1, 1, &argument, &result) == GOAT_NATIVE_RESOURCE_LIMIT;
        task->success &= !memcmp(&original, &result, sizeof(result));
        argument.value.integer = 3;
        task->success &= task->entry->invoke(1, 1, &argument, &result) == GOAT_NATIVE_OK;
        task->success &=
            result.type == GOAT_NATIVE_I64 && !result.reserved && result.value.integer == 3;
    }
    return NULL;
}

static bool adapter_guards(void) {
    bytecode_t *code =
        compile(L"const f=func(n){if(n<=0)return 0;return f(n-1)+1;};var x=f(3);", compiler);
    ASSERT(code && preparation == NATIVE_PREPARE_READY);
    const goat_native_entry_v1_t *entry = recursive_entry(code);
    ASSERT(entry);
    adapter_task_t tasks[4];
    pthread_t threads[4];
    for (size_t i = 0; i < 4; i++) {
        tasks[i] = (adapter_task_t){.entry = entry};
        ASSERT(!pthread_create(&threads[i], NULL, adapter_worker, &tasks[i]));
    }
    for (size_t i = 0; i < 4; i++) {
        ASSERT(!pthread_join(threads[i], NULL));
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
    task->success = !native_stack_has_headroom() && !run(task->proc, task->code)
                    && integer(task->proc, L"x", 7) && counters(task->proc, 0, 0, 1);
    return NULL;
}

static bool small_stack(void) {
    bytecode_t *code = compile(L"const f=func(n){return n;};var x=f(7);func(){x;};", compiler);
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
            compile(L"const f=func(n){return n+1;};var x=f(7);func(){x;};", commands[i]);
        ASSERT(code && preparation == statuses[i] && !code->native_functions);
        process_t *proc = create_process();
        ASSERT(!run(proc, code) && integer(proc, L"x", 8) && counters(proc, 0, 0, 0));
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

static bool empty(void) {
    bytecode_t *code = compile(L"var x=7;", "goat-missing-compiler-7f01");
    ASSERT(code && preparation == NATIVE_PREPARE_EMPTY && !code->native_functions);
    free_bytecode(code);
    return true;
}

static bool large_frame(void) {
    string_builder_t source;
    init_string_builder(&source, 512);
    append_string(&source, L"const f=func(n){var x=n;");
    for (size_t i = 0; i < 80; i++)
        append_string(&source, L"x=x+n;");
    string_value_t text = append_string(&source, L"return x;};var value=f(1);func(){value;};");
    bytecode_t *code = compile(text.data, "goat-missing-compiler-7f01");
    FREE_STRING(text);
    ASSERT(code && preparation == NATIVE_PREPARE_EMPTY && omitted && !code->native_functions);
    process_t *proc = create_process();
    ASSERT(!run(proc, code) && integer(proc, L"value", 81));
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
        {"generated numeric dispatch after graph destruction", dispatch},
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
            fprintf(stderr, "Allocation balance changed in %s\n", tests[i].name);
            ok = false;
        }
        test_output_case(ok, "%s", tests[i].name);
        passed += ok;
    }
    size_t failed = sizeof(tests) / sizeof(*tests) - passed;
    test_output_summary("Native pipeline", passed, failed);
    return !failed;
}
