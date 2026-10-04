/** @file test_native_call.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native selection, bytecode fallback and descriptor ownership through FUNC/CALL.
 */
#include "test_native_call.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "test_output.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static native_library_t *library;

static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, code, data);
    for (size_t i = 0; i < get_node_child_count(node); i++)
        deferred(get_node_child(node, i), code, data);
}

/** @brief Releases every analysis arena before any VM execution. */
static bytecode_t *compile(const wchar_t *source) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    bytecode_t *result = NULL;
    if (root && !analyze(root, &memory, options, NULL)) {
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        deferred(root, code, data);
        result = link_code_and_data(code, data);
        destroy_code_builder(code);
        destroy_data_builder(data);
    }
    destroy_options(options);
    destroy_arena(arena);
    return result;
}

static instr_index_t function_instruction(const bytecode_t *code, size_t ordinal) {
    for (size_t i = 0; i < code->instructions_count; i++)
        if (code->instructions[i].opcode == FUNC && !ordinal--)
            return i;
    return BAD_INSTR_INDEX;
}

static bool bind_function(bytecode_t *code, size_t ordinal, uint64_t id) {
    native_function_descriptor_t *function = create_native_function_descriptor(library, id);
    bool result =
        function
        && bind_bytecode_native_function(code, function_instruction(code, ordinal), function);
    release_native_function_descriptor(function);
    return result;
}

static int64_t calls(void) {
    const goat_native_entry_v1_t *entry = get_native_library_entry(library, 7);
    goat_native_value_v1_t value = {0};
    if (!entry || entry->invoke(1, 0, NULL, &value))
        return -1;
    return value.value.integer;
}

static object_t *variable(process_t *proc, const wchar_t *name) {
    object_t *key = create_string_object(proc, (string_value_t){name, wcslen(name), false});
    object_t *value = get_object_property(proc->main_thread->context->data, key);
    DECREF(key);
    return value;
}

static bool integer_variable(process_t *proc, const wchar_t *name, int64_t expected) {
    object_t *value = variable(proc, name);
    return value && is_integer_object(value) && get_object_integer_value(value).value == expected;
}

static bool metadata(void) {
    bytecode_t *code = compile(L"var f=func(n){return n;};var g=func(){return 42;};");
    ASSERT(code && !code->native_functions);
    instr_index_t instruction = function_instruction(code, 0);
    native_function_descriptor_t *function = create_native_function_descriptor(library, 1);
    ASSERT(!bind_bytecode_native_function(NULL, 0, function));
    ASSERT(!bind_bytecode_native_function(code, code->instructions_count, function));
    ASSERT(!bind_bytecode_native_function(code, instruction - 1, function));
    ASSERT(!bind_bytecode_native_function(code, function_instruction(code, 1), function));
    void *copy = ALLOC(code->buffer_size);
    memcpy(copy, code->buffer, code->buffer_size);
    ASSERT(bind_bytecode_native_function(code, instruction, function));
    ASSERT(bind_bytecode_native_function(code, instruction, function));
    release_native_function_descriptor(function);
    ASSERT(!memcmp(copy, code->buffer, code->buffer_size));
    ASSERT(get_bytecode_native_function(code, instruction) == function);
    ASSERT(!get_bytecode_native_function(code, code->instructions_count));
    ASSERT(bind_bytecode_native_function(code, instruction, NULL));
    ASSERT(!get_bytecode_native_function(code, instruction));
    FREE(copy);
    free_bytecode(code);
    return true;
}

static bool exact_types(void) {
    bytecode_t *code =
        compile(L"var f=func(n){return n;};var g=func(a,b){return a-b;};"
                L"var z=func(){return 42;};var a=f(10);var b=f(11);var c=f(10.0);"
                L"var d=g(7,0.5);var e=g(0.5,7);var q=z();var h=g(7,2);var j=g(7.0,2.0);");
    ASSERT(code && bind_function(code, 0, 1) && bind_function(code, 1, 2)
           && bind_function(code, 2, 3));
    int64_t before = calls();
    process_t *proc = create_process();
    context_t *initial = proc->main_thread->context;
    ASSERT(!run(proc, code));
    ASSERT(calls() == before + 6);
    ASSERT(proc->main_thread->context == initial && !proc->main_thread->data_stack->size);
    ASSERT(integer_variable(proc, L"a", 10) && integer_variable(proc, L"b", 11));
    ASSERT(is_real_object(variable(proc, L"c"))
           && get_object_real_value(variable(proc, L"c")).value == 10);
    ASSERT(get_object_real_value(variable(proc, L"d")).value == 6.5);
    ASSERT(get_object_real_value(variable(proc, L"e")).value == -6.5);
    ASSERT(integer_variable(proc, L"q", 42) && integer_variable(proc, L"h", 5));
    ASSERT(is_real_object(variable(proc, L"j"))
           && get_object_real_value(variable(proc, L"j")).value == 5);
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool fallback(void) {
    bytecode_t *code = compile(L"var f=func(n){return n;};var a=f();var b=f(true);"
                               L"var c=f(\"text\");var d=f({});var e=f(10.0);var q=f(10);"
                               L"var plain=func(n){return n+1;};var r=plain(4);");
    ASSERT(code && bind_function(code, 0, 5));
    int64_t before = calls();
    process_t *proc = create_process();
    ASSERT(!run(proc, code) && calls() == before + 1);
    ASSERT(variable(proc, L"a") == get_null_object());
    ASSERT(variable(proc, L"b") == get_boolean_object(true));
    ASSERT(variable(proc, L"c")->vtbl->type == TYPE_STRING);
    ASSERT(variable(proc, L"d")->vtbl->type == TYPE_USER_DEFINED_OBJECT);
    ASSERT(is_real_object(variable(proc, L"e")));
    ASSERT(integer_variable(proc, L"q", 10) && integer_variable(proc, L"r", 5));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool evaluation_and_identity(void) {
    bytecode_t *code =
        compile(L"var trace=0;var tick=func(n){trace=trace*10+n;return n;};"
                L"var f=func(n){return n;};var alias=f;"
                L"var a=alias(tick(1),tick(2));var first=trace;trace=0;"
                L"var b=alias(true,tick(3));var second=trace;"
                L"f=func(n){return n+100;};var c=f(1);var d=alias(2);"
                L"var print=alias;var e=print(3);var z=func(){return 42;};var q=z(tick(4));"
                L"var builtin_result=abs(-3);");
    ASSERT(code && bind_function(code, 1, 1) && bind_function(code, 3, 3));
    int64_t before = calls();
    process_t *proc = create_process();
    ASSERT(!run(proc, code) && calls() == before + 4);
    ASSERT(integer_variable(proc, L"first", 21) && integer_variable(proc, L"second", 3));
    ASSERT(integer_variable(proc, L"trace", 34));
    ASSERT(integer_variable(proc, L"a", 1) && variable(proc, L"b") == get_boolean_object(true));
    ASSERT(integer_variable(proc, L"c", 101) && integer_variable(proc, L"d", 2));
    ASSERT(integer_variable(proc, L"e", 3) && integer_variable(proc, L"q", 42));
    ASSERT(integer_variable(proc, L"builtin_result", 3));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static bool native_without_context(void) {
    bytecode_t *code = compile(L"var f=func(n){return n;};");
    ASSERT(code && bind_function(code, 0, 1));
    process_t *proc = create_process();
    ASSERT(!run(proc, code));
    object_t *function = variable(proc, L"f");
    /* Function keeps its descriptor after bytecode metadata has been destroyed. */
    free_bytecode(code);
    thread_t *thread = proc->main_thread;
    context_t *context = thread->context;
    double reals[] = {-0.0, 0.0, 10.0, 0x1p63, INFINITY, -INFINITY, NAN, 0x1p-1074};
    int64_t integers[] = {0, -1, INT64_MIN, INT64_MAX, INT64_C(9007199254740993)};
    int64_t before = calls();
    for (size_t i = 0; i < 13; i++) {
        object_t *arg = i < 8 ? create_real_number_object(proc, reals[i])
                              : create_integer_object(proc, integers[i - 8]);
        push_object_onto_stack(thread->data_stack, create_integer_object(proc, 73));
        push_object_onto_stack(thread->data_stack,
                               create_string_object(proc, STATIC_STRING(L"ignored")));
        push_object_onto_stack(thread->data_stack, arg);
        thread->instr_id = 123;
        ASSERT(call_object(function, 2, thread));
        ASSERT(thread->context == context && thread->instr_id == 124
               && thread->data_stack->size == 2);
        object_t *result = pop_object_from_stack(thread->data_stack);
        if (i < 8) {
            ASSERT(is_real_object(result));
            double real = get_object_real_value(result).value;
            ASSERT(isnan(reals[i]) ? isnan(real)
                                   : real == reals[i] && !!signbit(real) == !!signbit(reals[i]));
        } else {
            ASSERT(is_integer_object(result)
                   && get_object_integer_value(result).value == integers[i - 8]);
        }
        DECREF(result);
        result = pop_object_from_stack(thread->data_stack);
        ASSERT(get_object_integer_value(result).value == 73);
        DECREF(result);
    }
    ASSERT(calls() == before + 13);
    destroy_process(proc);
    return true;
}

static bool backend_failures(void) {
    for (unsigned mode = 0; mode <= 8; mode++) {
        wchar_t source[240];
        swprintf(source,
                 240,
                 L"var f=func(n){throw 123;};var caught=false;"
                 L"try {f(%u);} catch(e){caught=true;}",
                 mode);
        bytecode_t *code = compile(source);
        ASSERT(code && bind_function(code, 0, 4));
        process_t *proc = create_process();
        int64_t before = calls();
        ASSERT(run(proc, code) != 0 && calls() == before + 1);
        thread_t *thread = proc->main_thread;
        ASSERT(!thread->exception.value);
        context_t *handler = thread->context;
        while (handler && handler->control_flow != FLOW_THROW)
            handler = handler->previous;
        ASSERT(handler);
        uint32_t expected = mode == 8                ? 99
                            : mode == 0 || mode >= 6 ? GOAT_NATIVE_BAD_REQUEST
                                                     : mode;
        ASSERT(thread->native_status == expected);
        ASSERT(thread->data_stack->size == 1);
        ASSERT(get_object_integer_value(peek_object_from_stack(thread->data_stack, 0)).value
               == mode);
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

static bool fallback_exception(void) {
    bytecode_t *code = compile(L"var f=func(n){throw n;};var caught=false;"
                               L"try {f(true);} catch(e){caught=e;}");
    ASSERT(code && bind_function(code, 0, 1));
    process_t *proc = create_process();
    int64_t before = calls();
    ASSERT(!run(proc, code) && calls() == before);
    ASSERT(variable(proc, L"caught") == get_boolean_object(true));
    ASSERT(!proc->main_thread->exception.value && !proc->main_thread->native_status);
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static long unload_count(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

static bool lifetime(const char *path, const char *marker) {
    for (int route = 0; route < 3; route++) {
        long before = unload_count(marker);
        native_library_result_t loaded = load_native_library(path);
        ASSERT(loaded.status == NATIVE_LIBRARY_OK);
        library = loaded.library;
        bytecode_t *code = compile(route == 1 ? L"var maker=func(){const local=func(n){return "
                                                L"n;};return local;};var f=maker();var alias=f;"
                                              : L"var f=func(n){return n;};var alias=f;");
        ASSERT(code && bind_function(code, route == 1 ? 1 : 0, 1));
        destroy_native_library_result(&loaded);
        library = NULL;
        process_t *proc = create_process();
        ASSERT(!run(proc, code));
        free_bytecode(code);
        collect_garbage(proc);
        ASSERT(unload_count(marker) == before);
        object_t *function = variable(proc, L"alias");
        push_object_onto_stack(proc->main_thread->data_stack, create_integer_object(proc, 888));
        ASSERT(call_object(function, 1, proc->main_thread));
        ASSERT(
            get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
            == 888);
        if (route < 2) {
            object_t *key = create_string_object(proc, STATIC_STRING(L"f"));
            ASSERT(set_object_property(proc->main_thread->context->data, key, get_null_object())
                   == MSTAT_OK);
            DECREF(key);
            ASSERT(unload_count(marker) == before);
            /* Route 0: final DECREF; route 1: an unreachable reference cycle. */
            key = create_string_object(proc, STATIC_STRING(L"alias"));
            ASSERT(set_object_property(proc->main_thread->context->data, key, get_null_object())
                   == MSTAT_OK);
            DECREF(key);
            if (route == 1) {
                ASSERT(unload_count(marker) == before);
                collect_garbage(proc);
            }
            ASSERT(unload_count(marker) == before + 1);
        }
        destroy_process(proc);
        ASSERT(unload_count(marker) == before + 1);
    }
    return true;
}

bool test_native_calls(const char *provider, const char *generated, const char *marker) {
    remove(marker);
    test_output_start("native call");
    native_library_result_t loaded = load_native_library(provider);
    if (loaded.status != NATIVE_LIBRARY_OK) {
        fprintf(stderr, "%s\n", loaded.diagnostic);
        destroy_native_library_result(&loaded);
        test_output_case(false, "load provider");
        test_output_summary("Native call", 0, 1);
        return false;
    }
    library = loaded.library;
    int passed = 0, failed = 0;

    const struct {
        const char *name;
        bool (*test)(void);
    } tests[] = {
        {"FUNC metadata validation and unchanged serialized bytes", metadata},
        {"exact numeric signatures and mixed argument order", exact_types},
        {"missing, nonnumeric and unavailable signatures fall back", fallback},
        {"evaluation order, extras, aliases, rebinding and builtins", evaluation_and_identity},
        {"native call preserves context, stack and numeric edges", native_without_context},
        {"backend failures stop without retry or Goat catch", backend_failures},
        {"unmatched signature preserves bytecode throw/catch", fallback_exception}};

    for (size_t i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
        size_t memory = get_allocated_memory_size();
        bool ok = tests[i].test();
        ok = ok && memory == get_allocated_memory_size();
        test_output_case(ok, "%s", tests[i].name);
        passed += ok;
        failed += !ok;
    }
    destroy_native_library_result(&loaded);
    library = NULL;
    size_t memory = get_allocated_memory_size();
    bool ok = lifetime(provider, marker);
    ok = ok && memory == get_allocated_memory_size();
    test_output_case(ok,
                     "library survives metadata, then unloads on DECREF, GC and process release");
    passed += ok;
    failed += !ok;
    loaded = load_native_library(generated);
    library = loaded.library;
    /* The generated provider has the same first three function identities. */
    if (loaded.status == NATIVE_LIBRARY_OK) {
        bytecode_t *code =
            compile(L"var f=func(n){return n;};var g=func(a,b){return a-b;};"
                    L"var z=func(){return 42;};var a=f(77);var b=f(2.5);var c=g(8,0.5);var d=z();");
        ok = code && bind_function(code, 0, 1) && bind_function(code, 1, 2)
             && bind_function(code, 2, 3);
        process_t *proc = create_process();
        /* Trap bytecode entry: a silently falling-back implementation must fail this test. */
        for (size_t i = 0; code && i < code->instructions_count; i++)
            if (code->instructions[i].opcode == FUNC)
                code->instructions[code->instructions[i - 1].arg1] = (instruction_t){.opcode = END};
        ok = ok && !run(proc, code) && integer_variable(proc, L"a", 77) && variable(proc, L"b")
             && is_real_object(variable(proc, L"b"))
             && get_object_real_value(variable(proc, L"b")).value == 2.5 && variable(proc, L"c")
             && get_object_real_value(variable(proc, L"c")).value == 7.5
             && integer_variable(proc, L"d", 42);
        if (code)
            free_bytecode(code);
        destroy_process(proc);
    } else {
        ok = false;
    }
    destroy_native_library_result(&loaded);
    library = NULL;
    test_output_case(ok, "generated adapters execute through CALL with bytecode bodies disabled");
    passed += ok;
    failed += !ok;
    test_output_summary("Native call", passed, failed);
    return failed == 0;
}
