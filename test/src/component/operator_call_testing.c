/** @file operator_call_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-VM callable object contracts (#501), also active in Release.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/analysis.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "common/compilation_error.h"
#include "common/module_compilation.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/exception.h"
#include "model/module_files.h"
#include "model/object_stack.h"
#include "model/process.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *group;
static size_t program_number;

static object_t *key(process_t *p, const gchar_t *name) {
    return create_string_object(p, (string_value_t){name, STRLEN(name), false});
}

static field_cell_t *field(process_t *p,
                           object_t *object,
                           const gchar_t *name,
                           object_t *value,
                           access_modifier_t access) {
    object_t *k = key(p, name);
    field_cell_t *cell = create_object_field(object,
                                             k,
                                             value,
                                             false,
                                             access,
                                             get_object_module(object),
                                             get_object_access_domain(object),
                                             true);
    DECREF(k);
    assert(cell);
    return cell;
}

static operation_result_t collect(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    assert(count == 0);
    collect_garbage(t->process);
    return operation_success(get_null_object());
}

static operation_result_t unexpected_plain(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    assert(!"operator builtin must receive the original receiver");
    return operation_success(get_null_object());
}

static operation_result_t probe(object_t *receiver, object_t **args, uint16_t count, thread_t *t) {
    assert(receiver->vtbl->type == TYPE_USER_DEFINED_OBJECT);
    assert(count <= 4);
    /* Both operands may be temporaries with no roots outside the in-flight call. */
    collect_garbage(t->process);
    object_t *k = key(t->process, U"marker");
    assert(get_object_property(receiver, k) == get_static_integer_object(81));
    for (size_t i = 0; i < count; ++i)
        assert(get_object_property(args[i], k) == get_static_integer_object(82 + i));
    DECREF(k);
    return operation_success(get_boolean_object(true));
}

static operation_result_t fail_builtin(object_t **args, uint16_t count, thread_t *t) {
    (void)t;
    assert(count == 1);
    INCREF(args[0]);
    return operation_exception(args[0]);
}

/* Plain builtin functions exercise the synchronous result-policy path. */
static operation_result_t boolean_builtin(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    return operation_success(get_boolean_object(true));
}

static operation_result_t nonboolean_builtin(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    return operation_success(get_null_object());
}

/* Functions have no writable language properties. Instrument the actual function
 * vtables to prove CALL/CALLM never even consult a function's property path. */
static object_vtbl_t *poisoned_vtables[2];
static object_t *(*original_getters[2])(const object_t *, const object_t *);
static unsigned poisoned_count;

static object_t *unexpected_function_property(const object_t *object, const object_t *name) {
    (void)object;
    (void)name;
    assert(!"ordinary function calls must not look up ()");
    return NULL;
}

static operation_result_t poison_function(object_t **args, uint16_t count, thread_t *t) {
    (void)t;
    assert(count == 1 && is_function_object(args[0]));
    assert(poisoned_count < 2);
    poisoned_vtables[poisoned_count] = args[0]->vtbl;
    original_getters[poisoned_count] = args[0]->vtbl->get_property;
    ++poisoned_count;
    args[0]->vtbl->get_property = unexpected_function_property;
    return operation_success(get_null_object());
}

static void restore_function_properties(void) {
    while (poisoned_count) {
        --poisoned_count;
        poisoned_vtables[poisoned_count]->get_property = original_getters[poisoned_count];
    }
}

static void setup(process_t *p) {
    static builtin_function_object_t poison_storage;
    static const builtin_function_t poison_descriptor = {.name = U"poison",
                                                         .execute = poison_function};
    field(p,
          p->main_thread->context->data,
          U"poison",
          get_builtin_function_object(&poison_storage, &poison_descriptor),
          ACCESS_EXTERNAL);
    static builtin_function_object_t gc_storage, probe_storage, fail_storage;
    static builtin_function_object_t boolean_storage, nonboolean_storage;
    static const builtin_function_t boolean_descriptor = {.name = U"booleanBuiltin",
                                                          .execute = boolean_builtin};
    static const builtin_function_t nonboolean_descriptor = {.name = U"nonbooleanBuiltin",
                                                             .execute = nonboolean_builtin};
    field(p,
          p->main_thread->context->data,
          U"booleanBuiltin",
          get_builtin_function_object(&boolean_storage, &boolean_descriptor),
          ACCESS_EXTERNAL);
    field(p,
          p->main_thread->context->data,
          U"nonbooleanBuiltin",
          get_builtin_function_object(&nonboolean_storage, &nonboolean_descriptor),
          ACCESS_EXTERNAL);
    static const builtin_function_t gc_descriptor = {.name = U"collect", .execute = collect};
    static const builtin_function_t probe_descriptor = {.name = U"probe",
                                                        .execute = unexpected_plain,
                                                        .execute_method = probe};
    static const builtin_function_t fail_descriptor = {.name = U"failBuiltin",
                                                       .min_args = 1,
                                                       .execute = fail_builtin};
    field(p,
          p->main_thread->context->data,
          U"collect",
          get_builtin_function_object(&gc_storage, &gc_descriptor),
          ACCESS_EXTERNAL);
    field(p,
          p->main_thread->context->data,
          U"probe",
          get_builtin_function_object(&probe_storage, &probe_descriptor),
          ACCESS_EXTERNAL);
    field(p,
          p->main_thread->context->data,
          U"failBuiltin",
          get_builtin_function_object(&fail_storage, &fail_descriptor),
          ACCESS_EXTERNAL);
    /* Property names with access metadata are constructed directly because the
     * language declaration syntax uses identifiers, not operator strings. */
    object_t *root = get_root_object();
    object_t *restricted = create_user_defined_object(p, (object_array_t){&root, 1});
    field(p, restricted, U"()", get_function_sign(), ACCESS_PRIVATE);
    field(p, p->main_thread->context->data, U"restricted", restricted, ACCESS_EXTERNAL);
    DECREF(restricted);
}

static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, code, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), code, data);
}

/* Every source case executes optimized/unoptimized, directly linked and after
 * file storage plus fresh decoding. No runtime wrappers or AST data are reused. */
static void program(const gchar_t *source, opcode_t required_opcode, unsigned failure) {
    ++program_number;
    string_value_t checked =
        format_string(failure ? U"%s" : U"%s\nif(!result) throw 9001;", source);
    for (unsigned optimized = 0; optimized < 2; ++optimized) {
        size_t before = get_allocated_memory_size();
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *node = parse_analysis_test_program(&memory, checked);
        if (!node)
            fprintf(stderr, "%s source %zu did not parse\n", group, program_number);
        assert(node);
        /* Reparse reconstructed source as well, including empty lists and chains. */
        string_value_t reconstructed = generate_goat_code_from_node(node);
        node = parse_analysis_test_program(&memory, reconstructed);
        FREE_STRING(reconstructed);
        assert(node);
        options_t *options = create_options();
        options->optimization_level = optimized ? OPTIMIZATION_ALL : OPTIMIZATION_NONE;
        process_t *analysis_process = create_process();
        setup(analysis_process);
        options->session_object = analysis_process->main_thread->context->data;
        compilation_error_t *errors = analyze(node, &memory, options, NULL);
        for (compilation_error_t *e = errors; e; e = e->next)
            assert(e->severity == WARNING);
        destroy_options(options);
        destroy_process(analysis_process);
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(node, builder, data);
        deferred(node, builder, data);
        bytecode_t *linked = link_code_and_data(builder, data);
        assert(linked && validate_bytecode_image(linked));
        if (required_opcode != NOP) {
            bool found = false;
            for (size_t i = 0; i < linked->instructions_count; ++i)
                found |= linked->instructions[i].opcode == required_opcode;
            assert(found); /* The expression must still use the existing opcode. */
        }
        void *image = malloc(linked->buffer_size);
        assert(image);
        memcpy(image, linked->buffer, linked->buffer_size);
        FILE *stored = tmpfile();
        assert(stored);
        assert(fwrite(image, 1, linked->buffer_size, stored) == linked->buffer_size);
        assert(fflush(stored) == 0 && fseek(stored, 0, SEEK_SET) == 0);
        assert(fread(image, 1, linked->buffer_size, stored) == linked->buffer_size);
        assert(fclose(stored) == 0);
        bytecode_t *loaded = decode_bytecode_image(image, linked->buffer_size);
        assert(loaded);
        for (unsigned serialized = 0; serialized < 2; ++serialized) {
            bytecode_t *code = serialized ? loaded : linked;
            process_t *p = create_process();
            thread_t *t = p->main_thread;
            context_t *caller = t->context;
            setup(p);
            int status = run(p, code);
            restore_function_properties();
            if (status && !failure && t->exception.value) {
                string_value_t exception = convert_object_to_string(t->exception.value);
                char *message = encode_utf8(exception.data);
                char *text = encode_utf8(source);
                fprintf(stderr, "Exception: %s\nSource: %s\n", message, text);
                FREE(message);
                FREE(text);
                FREE_STRING(exception);
            }
            if (failure == 3) {
                /* Builtin non-callables retain the pre-existing terminal failure. */
                assert(status != 0 && !t->exception.value);
            } else if (failure) {
                object_t *expected = failure == 1 ? get_static_integer_object(73)
                                                  : get_exception_invalid_operation();
                assert(status != 0 && t->exception.value == expected);
                assert(exception_trace_size(t->exception.trace) >= 2);
                const exception_trace_frame_t *origin =
                    exception_trace_frame(t->exception.trace, 0);
                assert(origin->instruction < code->instructions_count);
                assert(code->instructions[origin->instruction].opcode
                       == (failure == 1 ? THROW : required_opcode));
                assert(t->data_stack->size == 0);
            } else {
                if (status)
                    fprintf(stderr,
                            "%s source %zu optimized=%u stored=%u failed\n",
                            group,
                            program_number,
                            optimized,
                            serialized);
                assert(status == 0 && !t->exception.value && t->context == caller);
                assert(t->data_stack->size == 0);
            }
            assert(!memcmp(image, code->buffer, code->buffer_size));
            destroy_process(p);
        }
        free(image);
        free_bytecode(loaded);
        free_bytecode(linked);
        destroy_code_builder(builder);
        destroy_data_builder(data);
        destroy_arena(arena);
        assert(get_allocated_memory_size() == before);
    }
    FREE_STRING(checked);
}

static object_t *object(process_t *p, object_t *proto) {
    return create_user_defined_object(p, (object_array_t){&proto, 1});
}

typedef struct {
    arena_t *arena;
    code_builder_t *builder;
    data_builder_t *data;
    bytecode_t *code;
} fixture_t;

static void dispose(fixture_t *f) {
    free_bytecode(f->code);
    destroy_code_builder(f->builder);
    destroy_data_builder(f->data);
    destroy_arena(f->arena);
}

static unsigned failed_rhs_calls;

static operation_result_t failed_rhs(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)t;
    assert(count == 0);
    ++failed_rhs_calls;
    return operation_success(get_boolean_object(true));
}

static bool
fail_initializer(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    (void)data;
    object_t *payload = object(module_file_process(file), get_root_object());
    *failure = (exception_t){payload, capture_exception_trace(frame, module_file_entry(file))};
    return false;
}

static unsigned failed_scope_handler_calls;

static operation_result_t failed_scope_handler(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    ++failed_scope_handler_calls;
    return operation_success(get_boolean_object(true));
}

/** Failed ordinary lookup is an error, not an absent operator. In particular, an
 * object used as a cached exception may inherit its failed initializer scope:
 * call selection must preserve this error after evaluating arguments. */
static void failed_file_case(void) {
    static builtin_function_object_t handler_storage, rhs_storage;
    static const builtin_function_t rhs_descriptor = {.name = U"rhs", .execute = failed_rhs};
    static const builtin_function_t handler_descriptor = {.name = U"failedScopeHandler",
                                                          .execute = failed_scope_handler};
    object_t *handler = get_builtin_function_object(&handler_storage, &handler_descriptor);
    char source[] = "external ready = true;";
    module_source_t file = {.path = "call_failure.goat",
                            .name = "call_failure.goat",
                            .source = source,
                            .length = sizeof(source) - 1};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compilation, &error));
    fixture_t f = {0};
    f.arena = create_arena(8);
    parser_memory_t memory = {f.arena, f.arena, f.arena, f.arena};
    node_t *node =
        parse_analysis_test_program(&memory, (string_value_t){U"target(rhs());", 14, false});
    assert(node);
    f.builder = create_code_builder();
    f.data = create_data_builder();
    generate_bytecode_from_node(node, f.builder, f.data);
    f.code = link_code_and_data(f.builder, f.data);
    void *image = malloc(f.code->buffer_size);
    assert(image);
    memcpy(image, f.code->buffer, f.code->buffer_size);
    bytecode_t *loaded = decode_bytecode_image(image, f.code->buffer_size);
    assert(loaded);
    for (unsigned decoded = 0; decoded < 2; ++decoded)
        for (unsigned derived_target = 0; derived_target < 2; ++derived_target)
            for (unsigned variant = 0; variant < 7; ++variant) {
                /* 0: absent; 1..3: function/nonfunction/private on failed scope;
                 * 4..6: own function/nonfunction/private on a healthy descendant. */
                if (!derived_target && variant >= 4)
                    continue;
                process_t *p = create_process();
                thread_t *t = p->main_thread;
                module_files_t *files = NULL;
                assert(create_module_files_from_snapshot(p,
                                                         compilation->metadata,
                                                         compilation->code,
                                                         &files));
                module_file_t *failed = module_files_main(files);
                object_t *scope = module_file_scope(failed);
                object_t *derived = object(p, scope);
                object_t *target = derived_target ? derived : scope;
                if (variant >= 1 && variant <= 3)
                    field(p,
                          scope,
                          U"()",
                          variant == 2 ? get_null_object() : handler,
                          variant == 3 ? ACCESS_PRIVATE : ACCESS_EXTERNAL);
                if (variant >= 4)
                    field(p,
                          derived,
                          U"()",
                          variant == 5 ? get_null_object() : handler,
                          variant == 6 ? ACCESS_PRIVATE : ACCESS_EXTERNAL);
                field(p, t->context->data, U"target", target, ACCESS_EXTERNAL);
                field(p,
                      t->context->data,
                      U"rhs",
                      get_builtin_function_object(&rhs_storage, &rhs_descriptor),
                      ACCESS_EXTERNAL);
                field(p, t->context->data, U"descendant", derived, ACCESS_EXTERNAL);
                object_t *result = NULL;
                exception_t original = {0};
                assert(initialize_module_file(failed,
                                              t->context,
                                              fail_initializer,
                                              NULL,
                                              &result,
                                              &original)
                       == MODULE_INITIALIZE_FAILED);
                assert(!result && original.value && original.trace);
                DECREF(derived); /* The caller context owns the live descendant. */
                failed_scope_handler_calls = 0;
                failed_rhs_calls = 0;
                int status = run(p, decoded ? loaded : f.code);
                assert(!memcmp(image, (decoded ? loaded : f.code)->buffer, f.code->buffer_size));
                if (variant == 4) {
                    assert(failed_rhs_calls == 1);
                    /* A successfully selected own handler never consults the ancestor. */
                    assert(status == 0 && !t->exception.value && failed_scope_handler_calls == 1);
                } else {
                    assert(status != 0 && failed_scope_handler_calls == 0 && failed_rhs_calls == 1);
                    if (variant == 5 || variant == 6) {
                        /* Neither an invalid nor inaccessible own handler falls through. */
                        assert(t->exception.value
                               == (variant == 5 ? get_exception_invalid_operation()
                                                : get_exception_access_denied()));
                        assert(t->exception.trace && t->exception.trace != original.trace);
                    } else {
                        /* Missing handlers on a failed scope cannot select builtin fallback.
                         */
                        assert(t->exception.value == original.value);
                        assert(t->exception.trace == original.trace);
                    }
                }
                assert(t->data_stack->size == 0);
                assert(module_file_exception(failed).value == original.value);
                assert(module_file_exception(failed).trace == original.trace);
                clear_exception(&original);
                destroy_process(p);
            }
    free(image);
    free_bytecode(loaded);
    dispose(&f);
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
}

static void matrix_case(void) {
    const gchar_t *args[] = {U"", U"11", U"11,22,33"};
    unsigned counts[] = {0, 1, 3};
    for (unsigned inherited = 0; inherited < 2; ++inherited)
        for (unsigned i = 0; i < 3; ++i) {
            string_value_t s = format_string(
                U"var result=false;const state={};state.saved=null;const proto={};"
                U"const a=%s;a.marker=81;proto[\"()\"]=func(x,y){collect();"
                U"if(this!=a || this.marker!=81 || arguments.length!=%u)throw 91;"
                U"if(arguments.length==0 && (x!=null || y!=null))throw 92;"
                U"if(arguments.length==1 && (x!=11 || y!=null))throw 93;"
                U"if(arguments.length==3 && (x!=11 || y!=22 || arguments[2]!=33))throw 94;"
                U"state.saved=arguments;x=null;collect();return this;};"
                U"const value=a(%s);collect();result=value==a && state.saved.length==%u;",
                inherited ? U"proto -> {}" : U"proto",
                counts[i],
                args[i],
                counts[i]);
            program(s.data, CALL, 0);
            FREE_STRING(s);
        }
    program(U"var result=false;const make=func(offset){const a={};"
            U"a[\"()\"]=func(n){return offset+n;};return a;};"
            U"result=make(40)(2)==42;",
            CALL,
            0);
    program(U"var result=false;const a={var secret=81;public getter=func(){return this.secret;};};"
            U"a[\"()\"]=a.getter;result=a()==81;",
            CALL,
            0);
}

static void order_case(void) {
    program(U"var result=false;const a={};a[\"()\"]=func(){throw 99;};"
            U"const arg=func(){a[\"()\"]=null;return 7;};"
            U"try {a(arg());}catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            CALL,
            0);
    program(U"var result=false;var log=0;const old={};var a=old;"
            U"old[\"()\"]=func(){throw 99;};"
            U"const callee=func(){log=1;return a;};"
            U"const first=func(){log=log*10+2;a={};return 11;};"
            U"const last=func(){log=log*10+3;old[\"()\"]=func(x,y){log=log*10+4;"
            U"return this==old && x==11 && y==22;};return 22;};"
            U"const value=callee()(first(),last());result=value && log==1234;",
            CALL,
            0);
    program(
        U"var result=false;const old={};var a=old;old[\"()\"]=func(x){return this==old && x==7;};"
        U"const arg=func(){a={};collect();return 7;};result=a(arg());",
        CALL,
        0);
    program(U"var result=false;const a={};const arg=func(){a[\"()\"]=func(x){return x==7;};"
            U"return 7;};result=a(arg());",
            CALL,
            0);
    program(U"var result=false;var log=0;const a={};a[\"()\"]=func(){log=9;};"
            U"const first=func(){log=1;throw 73;};const later=func(){log=9;};"
            U"try {a(first(),later());}catch(e){result=e==73 && log==1;}",
            CALL,
            0);
    program(U"var result=false;var log=0;const callee=func(){throw 73;};"
            U"const arg=func(){log=9;};try {callee()(arg());}catch(e){result=e==73 && log==0;}",
            CALL,
            0);
}

static void property_case(void) {
    const gchar_t *access[] = {U"holder.value", U"holder[\"value\"]", U"items[0]", U"provider[]"};
    for (unsigned i = 0; i < 4; ++i) {
        string_value_t s =
            format_string(U"var result=false;const a={};a[\"()\"]=func(x){return this==a && x==7;};"
                          U"const holder={};holder.value=a;const items=[a];const provider={};"
                          U"provider[\"[]\"]=func(){return a;};result=%s(7);",
                          access[i]);
        program(s.data, CALLM, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;const holder={};const a={};holder.value=a;"
            U"a[\"()\"]=func(){throw 99;};const arg=func(){holder.value={};"
            U"a[\"()\"]=func(x){return this==a && x==7;};collect();return 7;};"
            U"result=holder.value(arg());",
            CALLM,
            0);
    program(U"var result=false;const holder={};const a={};holder.value=a;"
            U"a[\"()\"]=func(x){return this==a && x==7;};"
            U"const arg=func(){holder.value={};collect();return 7;};"
            U"result=holder[\"value\"](arg());",
            CALLM,
            0);
    /* Direct functions and methods must retain their normal receiver semantics. */
    program(U"var result=false;const f=func(x){return x+1;};const holder={};"
            U"holder.marker=81;holder.method=func(x){return this.marker+x;};"
            U"result=f(6)==7 && holder.method(1)==82 && holder[\"method\"](2)==83;",
            CALLM,
            0);
}

static void bypass_case(void) {
    program(U"var result=false;const f=func(x){return x+1;};const holder={};"
            U"holder.marker=81;holder.method=func(x){return this.marker+x;};"
            U"holder.builtin=sign;poison(f);poison(sign);"
            U"result=f(6)==7 && holder.method(1)==82 && holder[\"method\"](2)==83 "
            U"&& sign(-5)==-1 && holder.builtin(4)==1;",
            CALLM,
            0);
}

static void lookup_case(void) {
    const gchar_t *bad[] = {U"null", U"false", U"3", U"\"x\"", U"[]", U"callable"};
    for (unsigned inherited = 0; inherited < 2; ++inherited)
        for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
            string_value_t s = format_string(
                U"var result=false;var effects=0;const proto={};const a=%s;"
                U"const callable={};callable[\"()\"]=func(){throw 99;};proto[\"()\"]=%s;"
                U"const arg=func(){effects+=1;return 7;};try {a(arg());}"
                U"catch(e){result=e==Exceptions.INVALID_OPERATION && effects==1;}",
                inherited ? U"proto -> {}" : U"proto",
                bad[i]);
            program(s.data, CALL, 0);
            FREE_STRING(s);
        }
    program(
        U"var result=false;const a={};try {a();}catch(e){result=e==Exceptions.INVALID_OPERATION;}",
        CALL,
        0);
    const gchar_t *values[] = {U"null", U"false", U"3", U"\"x\"", U"[]"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        string_value_t s = format_string(U"const a=%s;a();", values[i]);
        program(s.data, CALL, 3);
        FREE_STRING(s);
    }
    for (unsigned inherited = 0; inherited < 2; ++inherited) {
        string_value_t s =
            format_string(U"var result=false;var effects=0;const a=%s;"
                          U"const arg=func(){effects=1;return 7;};try {a(arg());}"
                          U"catch(e){result=e==Exceptions.ACCESS_DENIED && effects==1;}",
                          inherited ? U"restricted -> {}" : U"restricted");
        program(s.data, CALL, 0);
        FREE_STRING(s);
    }
}

static void lifetime_case(void) {
    program(U"var result=false;const make=func(){const a={};a.marker=81;"
            U"a[\"()\"]=func(x){collect();return this.marker==81 && x==null;};return a;};"
            U"result=make()(collect());",
            CALL,
            0);
    program(U"var result=false;const make=func(){const a={};a.marker=81;a[\"()\"]=probe;return a;};"
            U"const arg=func(n){const b={};b.marker=n;return b;};"
            U"result=make()(arg(82),arg(83),arg(84));",
            CALL,
            0);
    program(U"var result=false;const a={};a[\"()\"]=nonbooleanBuiltin;"
            U"const b={};b[\"()\"]=booleanBuiltin;result=a()==null && b();",
            CALL,
            0);
    program(U"var result=false;const a={};const payload={};a[\"()\"]=failBuiltin;"
            U"try {a(payload);}catch(e){result=e==payload;}",
            CALL,
            0);
    program(U"var result=false;const a={};a[\"()\"]=failBuiltin;"
            U"try {a();}catch(e){result=e==Exceptions.INVALID_ARGUMENT;}",
            CALL,
            0);
    program(U"var result=false;const a={};a[\"()\"]=sign;result=a(-5)==-1;", CALL, 0);
}

static void continuation_case(void) {
    program(U"var result=false;var log=0;const a={};const b={};"
            U"b[\"()\"]=func(x){collect();return x+1;};"
            U"a[\"()\"]=func(x){try {return b(x)+1;}finally {log+=100;collect();}};"
            U"const outer=func(x,y,z){return x==97 && y==9 && z==98;};"
            U"result=outer(97,a(7),98) && log==100;",
            CALL,
            0);
    program(U"var result=false;var log=0;const a={};const payload={};"
            U"a[\"()\"]=func(){try {log=1;throw payload;}finally {log+=10;collect();}};"
            U"var assigned=99;try {assigned=a();}"
            U"catch(e){result=e==payload && log==11 && assigned==99;}collect();",
            CALL,
            0);
    program(U"var result=false;const a={};a[\"()\"]=func(){try {return 99;}finally {return 88;}};"
            U"result=a()==88;",
            CALL,
            0);
    program(U"const a={};a[\"()\"]=func(){const nested=func(){throw 73;};nested();};a();", CALL, 1);
    program(U"const a={};const outer=func(){a();};outer();", CALL, 2);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    group = argv[1];
    if (!strcmp(group, "matrix"))
        matrix_case();
    else if (!strcmp(group, "order"))
        order_case();
    else if (!strcmp(group, "property"))
        property_case();
    else if (!strcmp(group, "bypass"))
        bypass_case();
    else if (!strcmp(group, "lookup"))
        lookup_case();
    else if (!strcmp(group, "lifetime"))
        lifetime_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "failed-file"))
        failed_file_case();
    else
        assert(!"unknown callable object case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
