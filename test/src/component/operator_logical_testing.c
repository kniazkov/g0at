/** @file operator_logical_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-VM logical operator contracts (#498), also active in Release.
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

typedef struct {
    const gchar_t *sign;
    const gchar_t *expression;
    opcode_t opcode;
    unsigned arguments;
} operator_case_t;

static const operator_case_t operators[] = {{U"&&", U"a && rhs()", LAND, 1},
                                            {U"||", U"a || rhs()", LOR, 1}};

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
    assert(count <= 1);
    /* Both operands may be temporaries with no roots outside the in-flight call. */
    collect_garbage(t->process);
    object_t *k = key(t->process, U"marker");
    assert(get_object_property(receiver, k) == get_static_integer_object(81));
    if (count)
        assert(get_object_property(args[0], k) == get_static_integer_object(82));
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

static void setup(process_t *p) {
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
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i)
        field(p, restricted, operators[i].sign, get_function_sign(), ACCESS_PRIVATE);
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
            if (status && !failure) {
                string_value_t exception = convert_object_to_string(t->exception.value);
                char *message = encode_utf8(exception.data);
                char *text = encode_utf8(source);
                fprintf(stderr, "Exception: %s\nSource: %s\n", message, text);
                FREE(message);
                FREE(text);
                FREE_STRING(exception);
            }
            if (failure) {
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

static void matrix_case(void) {
    /* Truthiness depends on own fields, not inherited operator properties. */
    for (size_t i = 0; i < 2; ++i)
        for (unsigned truth = 0; truth < 2; ++truth) {
            string_value_t source = format_string(
                U"var result=false;const state={};state.log=0;const proto={};proto.marker=81;"
                U"proto[\"%s\"]=func(x){state.log=state.log*10+3;"
                U"if(this!=a || this.marker!=81 || arguments.length!=1 || x!=7)throw 94;"
                U"return %s;};const a=proto -> {};if(!!a!=false)throw 95;"
                U"const left=func(){state.log=state.log*10+1;return a;};"
                U"const rhs=func(){state.log=state.log*10+2;return 7;};"
                U"const value=left() %s rhs();result=value==%s && state.log==123;",
                operators[i].sign,
                truth ? U"true" : U"false",
                operators[i].sign,
                truth ? U"true" : U"false");
            program(source.data, operators[i].opcode, 0);
            FREE_STRING(source);
        }
    for (size_t i = 0; i < 2; ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (unsigned truth = 0; truth < 2; ++truth) {
                const operator_case_t *op = &operators[i];
                string_value_t source = format_string(
                    U"var result=false; const state={};state.log=0; const proto={}; const a=%s; "
                    U"a.marker=81; "
                    U"%s[\"%s\"]=func(x){state.log=state.log*10+3;return this.marker==81 "
                    U"&& arguments.length==1 && x.marker==82 && arguments[0]==x && %s;}; "
                    U"const left=func(){state.log=state.log*10+1;return a;}; "
                    U"const rhs=func(){state.log=state.log*10+2;const b={};b.marker=82;return b;}; "
                    U"const value=left() %s rhs();if(state.log!=123)throw "
                    U"state.log;result=value==%s && state.log==123;",
                    inherited ? U"proto -> {}" : U"{}",
                    inherited ? U"proto" : U"a",
                    op->sign,
                    truth ? U"true" : U"false",
                    op->sign,
                    truth ? U"true" : U"false");
                program(source.data, op->opcode, 0);
                FREE_STRING(source);
            }
    for (size_t i = 0; i < 2; ++i) {
        string_value_t s =
            format_string(U"var result=false; const a={}; a[\"%s\"]=func(x){return x==null "
                          U"&& arguments.length==1 && arguments[0]==null;}; result=a %s null;",
                          operators[i].sign,
                          operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
    }
}

static void fallback_case(void) {
    const gchar_t *values[] = {U"false", U"null", U"0", U"\"\"", U"{}", U"true", U"1", U"\"x\""};
    /* Empty user objects are false; an ordinary nonempty object is true. */
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < sizeof(values) / sizeof(*values); ++j) {
            unsigned truth = j >= 5;
            unsigned called = i ? !truth : truth;
            string_value_t s =
                format_string(U"var result=false; var calls=0; const a=%s; "
                              U"const rhs=func(){calls+=1;return 17;}; const value=%s; "
                              U"result=value==%s && calls==%u;",
                              values[j],
                              operators[i].expression,
                              (i || truth) ? U"true" : U"false",
                              called);
            program(s.data, NOP, 0);
            FREE_STRING(s);
        }
    program(U"var result=false; const a={}; a.marker=1; a[\"!\"]=func(){throw 99;}; "
            U"a[\"!!\"]=a[\"!\"]; var count=0; if(a) count+=1; while(a){count+=1;break;} "
            U"result=!!a && (a&&true) && (a||false) && count==2;",
            BOOL,
            0);
    program(U"var result=false; const a={}; a[\"||\"]=func(){throw 99;}; "
            U"const b={}; b[\"&&\"]=a[\"||\"]; result=(a&&true) && (b||false);",
            LAND,
            0);
    program(U"var result=false; const right={}; right[\"&&\"]=func(){throw 99;}; "
            U"right[\"||\"]=right[\"&&\"]; result=(true&&right) && (false||right);",
            NOP,
            0);
}

static void lookup_case(void) {
    const gchar_t *bad[] = {U"null", U"false", U"3", U"\"x\"", U"[]", U"callable"};
    for (size_t i = 0; i < 2; ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); ++j) {
                string_value_t s = format_string(
                    U"var result=false; var calls=0; const proto={}; const a=proto -> {}; "
                    U"const callable={}; callable[\"()\"]=func(){throw 91;}; "
                    U"proto[\"%s\"]=func(){throw 92;}; %s[\"%s\"]=%s; "
                    U"%s const rhs=func(){calls+=1;throw 93;}; try {%s;} "
                    U"catch(e){result=e==Exceptions.INVALID_OPERATION && calls==0;}",
                    operators[i].sign,
                    inherited ? U"proto" : U"a",
                    operators[i].sign,
                    bad[j],
                    inherited ? U"if(!!a!=false)throw 95;" : U"",
                    operators[i].expression);
                program(s.data, operators[i].opcode, 0);
                FREE_STRING(s);
            }
}

static void access_case(void) {
    for (size_t i = 0; i < 2; ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            string_value_t s = format_string(
                U"var result=false; var calls=0; const a=%s; const rhs=func(){calls+=1;throw 99;}; "
                U"%s try {%s;} catch(e){result=e==Exceptions.ACCESS_DENIED && calls==0;}",
                inherited ? U"restricted -> {}" : U"restricted",
                inherited ? U"if(!!a!=false)throw 95;" : U"",
                operators[i].expression);
            program(s.data, operators[i].opcode, 0);
            FREE_STRING(s);
        }
}

static void order_case(void) {
    for (size_t i = 0; i < 2; ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            string_value_t s = format_string(
                U"var result=false; const state={};state.calls=0; const proto={}; var a=proto -> "
                U"{}; "
                U"a.marker=81; const original=a; %s[\"%s\"]=func(x){state.calls+=1; "
                U"return this==original && this.marker==81 && x==7;}; "
                U"const rhs=func(){%s[\"%s\"]=func(){state.calls+=10;return false;}; "
                U"a={};collect();return 7;}; const value=%s; const next=original %s 7; "
                U"if(state.calls!=11)throw state.calls;result=value && !next && state.calls==11;",
                inherited ? U"proto" : U"a",
                operators[i].sign,
                inherited ? U"proto" : U"a",
                operators[i].sign,
                operators[i].expression,
                operators[i].sign);
            program(s.data, operators[i].opcode, 0);
            FREE_STRING(s);
        }
    /* A missing handler selects fallback permanently even if RHS installs one. */
    program(U"var result=false; const a={};a.marker=1; const state={};state.calls=0; "
            U"const rhs=func(){a[\"&&\"]=func(){state.calls+=1;return false;};return true;}; "
            U"const first=a&&rhs();const second=a&&true;result=first && !second && state.calls==1;",
            LAND,
            0);
    program(U"var result=false; const a={}; const state={};state.calls=0; const rhs=func(){"
            U"a[\"||\"]=func(){state.calls+=1;return false;};return true;}; "
            U"const first=a||rhs();const second=a||true;result=first && !second && state.calls==1;",
            LOR,
            0);
}

static void invalid_case(void) {
    const gchar_t *values[] =
        {U"null", U"0", U"1", U"2.5", U"\"\"", U"\"x\"", U"[]", U"{}", U"func(){return true;}"};
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < sizeof(values) / sizeof(*values); ++j) {
            string_value_t s = format_string(
                U"var result=false; var calls=0; const a={}; a[\"%s\"]=func(x){"
                U"calls+=10;return %s;}; const rhs=func(){calls+=1;return 7;}; "
                U"try {%s;} catch(e){result=e==Exceptions.INVALID_OPERATION && calls==11;}",
                operators[i].sign,
                values[j],
                operators[i].expression);
            program(s.data, operators[i].opcode, 0);
            FREE_STRING(s);
        }
    for (size_t i = 0; i < 2; ++i) {
        string_value_t s =
            format_string(U"var result=false;const a={}; a[\"%s\"]=func(){}; "
                          U"try {a %s true;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                          operators[i].sign,
                          operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
    }
}

static void continuation_case(void) {
    for (size_t i = 0; i < 2; ++i) {
        const gchar_t *outer = operators[i].sign;
        const gchar_t *inner = operators[1 - i].sign;
        string_value_t nested =
            format_string(U"var result=false;const state={};state.log=0;const a={};const b={};"
                          U"a[\"%s\"]=func(x){state.log=state.log*10+3;collect();return x;};"
                          U"b[\"%s\"]=func(x){state.log=state.log*10+2;collect();return x;};"
                          U"const rhs=func(){state.log=state.log*10+1;collect();return true;};"
                          U"const value=a %s (b %s rhs());result=value && state.log==123;",
                          outer,
                          inner,
                          outer,
                          inner);
        program(nested.data, operators[i].opcode, 0);
        FREE_STRING(nested);
        nested = format_string(
            U"var result=false;const state={};state.calls=0;const a={};"
            U"a[\"%s\"]=func(x){state.calls+=1;collect();return x;};"
            U"const rhs=func(){throw 99;};const first=a %s (false&&rhs());"
            U"const second=a %s (true||rhs());const b={};b.marker=1;"
            U"const third=a %s !!b;result=!first && second && third && state.calls==3;",
            outer,
            outer,
            outer,
            outer);
        program(nested.data, operators[i].opcode, 0);
        FREE_STRING(nested);
        nested = format_string(
            U"var result=false;const state={};state.log=0;const a={};"
            U"a[\"%s\"]=func(x){state.log=state.log*10+3;collect();return x;};"
            U"const rhs=func(){try {try {throw 7;} catch(e){state.log=1;collect();return e==7;}}"
            U"finally {state.log=state.log*10+2;collect();}};"
            U"const value=a %s rhs();result=value && state.log==123;",
            outer,
            outer);
        program(nested.data, operators[i].opcode, 0);
        FREE_STRING(nested);
    }
    for (size_t i = 0; i < 2; ++i) {
        string_value_t s =
            format_string(U"var result=false;var log=0; const a={}; const thrown={}; "
                          U"a[\"%s\"]=func(x){log=99;return true;}; "
                          U"const rhs=func(){try {throw thrown;} finally {log=1;collect();}}; "
                          U"try {%s;} catch(e){result=e==thrown && log==1;} collect();",
                          operators[i].sign,
                          operators[i].expression);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
        s = format_string(U"var result=false;var log=0;const a={};const thrown={};"
                          U"a[\"%s\"]=func(x){try {throw thrown;} finally {log=2;collect();}}; "
                          U"const rhs=func(){log=1;return 7;}; try {%s;} "
                          U"catch(e){result=e==thrown && log==2;} collect();",
                          operators[i].sign,
                          operators[i].expression);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
        s = format_string(U"var result=false; const a={};a[\"%s\"]=func(){"
                          U"try {return null;} finally {return true;}}; result=a %s null;",
                          operators[i].sign,
                          operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
    }
    program(U"var result=false; var log=0;const a={};const b={};"
            U"b[\"||\"]=func(x){log+=10;collect();return x;};"
            U"a[\"&&\"]=func(x){try {log+=1;return b||x;} finally {log+=100;collect();}};"
            U"const outer=func(x,y,z){return x==97 && y && z==98;};"
            U"result=outer(97,a&&true,98) && log==111;",
            LAND,
            0);
    program(U"const a={};a[\"&&\"]=func(){const nested=func(){throw 73;};return nested();};"
            U"a&&true;",
            LAND,
            1);
    program(U"var result=false;var effects=0;const left=func(){throw 8;};"
            U"const rhs=func(){effects+=1;return true;};try {left()||rhs();}"
            U"catch(e){result=e==8 && effects==0;}",
            LOR,
            0);
    program(U"var result=false;var log=0;const a={};a[\"||\"]=func(){return null;};"
            U"const inner=func(){try {return a||true;} finally {log=1;collect();}};"
            U"const outer=func(x,y){throw 99;};try {outer(97,inner());}"
            U"catch(e){result=e==Exceptions.INVALID_OPERATION && log==1;}collect();",
            LOR,
            0);
}

static void lifetime_case(void) {
    program(U"const a={};a[\"&&\"]=func(){return null;};"
            U"const inner=func(){return a&&true;};const outer=func(x,y){return x+y;};"
            U"outer(97,inner());",
            BOOL,
            2);

    for (size_t i = 0; i < 2; ++i) {
        string_value_t s = format_string(
            U"var result=false;var held=null;const make=func(){const a={};a.marker=81;"
            U"a[\"%s\"]=func(x){collect();const self=this;const nested=func(){"
            U"collect();return self.marker+x.marker==163;};return nested();};held=a;return a;};"
            U"const rhs=func(){held[\"%s\"]=null;held=null;collect();const b={};b.marker=82;"
            U"return b;};result=make() %s rhs();",
            operators[i].sign,
            operators[i].sign,
            operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
        s = format_string(
            U"var result=false;const make=func(){const a={};a.marker=81;a[\"%s\"]=probe;return a;};"
            U"const rhs=func(){collect();const b={};b.marker=82;return b;};result=make() %s rhs();",
            operators[i].sign,
            operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;var count=0;const a={};a[\"&&\"]=func(x){"
            U"count+=1;collect();if(x==0)return true;return this&&(x-1);};"
            U"result=(a&&40) && count==41;",
            LAND,
            0);
}

static void builtin_case(void) {
    for (size_t i = 0; i < 2; ++i) {
        string_value_t s = format_string(
            U"var result=false;const a={};a[\"%s\"]=booleanBuiltin;const value=a %s null;"
            U"a[\"%s\"]=nonbooleanBuiltin;try {a %s null;}"
            U"catch(e){result=value && e==Exceptions.INVALID_OPERATION;}",
            operators[i].sign,
            operators[i].sign,
            operators[i].sign,
            operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
        s = format_string(U"var result=false;const a={};const payload={};a[\"%s\"]=failBuiltin;"
                          U"try {a %s payload;}catch(e){result=e==payload;}",
                          operators[i].sign,
                          operators[i].sign);
        program(s.data, operators[i].opcode, 0);
        FREE_STRING(s);
    }
}

/* Decoder rejects unknown BOOL modes. Execution never writes the immutable image. */
static void wire_case(void) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        code_builder_t *b = create_code_builder();
        data_builder_t *d = create_data_builder();
        if (mode) {
            add_instruction(b, (instruction_t){.opcode = NIL});
            add_instruction(b, (instruction_t){.opcode = NIL});
        }
        add_instruction(b, (instruction_t){.opcode = TRUE});
        add_instruction(b, (instruction_t){.opcode = BOOL, .arg0 = mode});
        add_instruction(b, (instruction_t){.opcode = POP});
        add_instruction(b, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(b, d);
        if (mode == 2) {
            assert(code && !validate_bytecode_image(code));
            assert(!decode_bytecode_image(code->buffer, code->buffer_size));
            free_bytecode(code);
        } else {
            assert(code && validate_bytecode_image(code));
            void *image = malloc(code->buffer_size);
            assert(image);
            memcpy(image, code->buffer, code->buffer_size);
            process_t *p = create_process();
            int status = run(p, code);
            assert(status == 0 && !p->main_thread->exception.value);
            assert(p->main_thread->data_stack->size == 0);
            assert(!memcmp(image, code->buffer, code->buffer_size));
            /* Corrupt only the scratch wire copy, never the linked image. */
            size_t bool_offset =
                (const uint8_t *)&code->instructions[mode ? 3 : 1] - (const uint8_t *)code->buffer;
            ((uint8_t *)image)[bool_offset + 2] = 2;
            assert(!decode_bytecode_image(image, code->buffer_size));
            destroy_process(p);
            free(image);
            free_bytecode(code);
        }
        destroy_code_builder(b);
        destroy_data_builder(d);
    }
    for (unsigned receiver = 0; receiver < 2; ++receiver) {
        code_builder_t *b = create_code_builder();
        data_builder_t *d = create_data_builder();
        add_instruction(b, (instruction_t){.opcode = receiver ? TRUE : NIL});
        add_instruction(b, (instruction_t){.opcode = receiver ? NIL : TRUE});
        add_instruction(b, (instruction_t){.opcode = TRUE});
        add_instruction(b, (instruction_t){.opcode = BOOL, .arg0 = 1});
        add_instruction(b, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(b, d);
        assert(code);
        process_t *p = create_process();
        assert(run(p, code) != 0);
        assert(p->main_thread->exception.value == get_exception_invalid_operation());
        assert(p->main_thread->data_stack->size == 0);
        destroy_process(p);
        free_bytecode(code);
        destroy_code_builder(b);
        destroy_data_builder(d);
    }
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
 * logical selection must preserve this error before evaluating the right operand. */
static void failed_file_case(void) {
    static const struct {
        const gchar_t *sign, *opposite, *source;
    } operations[] = {{U"&&", NULL, U"target && rhs();"}, {U"||", NULL, U"target || rhs();"}};

    static builtin_function_object_t handler_storage, rhs_storage;
    static const builtin_function_t rhs_descriptor = {.name = U"rhs", .execute = failed_rhs};
    static const builtin_function_t handler_descriptor = {.name = U"failedScopeHandler",
                                                          .execute = failed_scope_handler};
    object_t *handler = get_builtin_function_object(&handler_storage, &handler_descriptor);
    char source[] = "external ready = true;";
    module_source_t file = {.path = "logical_failure.goat",
                            .name = "logical_failure.goat",
                            .source = source,
                            .length = sizeof(source) - 1};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compilation, &error));
    for (size_t op = 0; op < sizeof(operations) / sizeof(*operations); ++op) {
        fixture_t f = {0};
        f.arena = create_arena(8);
        parser_memory_t memory = {f.arena, f.arena, f.arena, f.arena};
        node_t *node = parse_analysis_test_program(
            &memory,
            (string_value_t){operations[op].source, STRLEN(operations[op].source), false});
        assert(node);
        f.builder = create_code_builder();
        f.data = create_data_builder();
        generate_bytecode_from_node(node, f.builder, f.data);
        f.code = link_code_and_data(f.builder, f.data);
        for (unsigned derived_target = 0; derived_target < 2; ++derived_target)
            for (unsigned variant = 0; variant < 9; ++variant) {
                /* 0: no operator anywhere; 1..4: requested/opposite/nonfunction/private
                 * on the failed scope; 5..8: own requested/nonfunction/private/opposite
                 * on a healthy descendant. These latter cases need a descendant. */
                if ((!derived_target && variant >= 5)
                    || ((variant == 2 || variant == 8) && !operations[op].opposite))
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
                if (variant >= 1 && variant <= 4)
                    field(p,
                          scope,
                          variant == 2 ? operations[op].opposite : operations[op].sign,
                          variant == 3 ? get_null_object() : handler,
                          variant == 4 ? ACCESS_PRIVATE : ACCESS_EXTERNAL);
                if (variant >= 5)
                    field(p,
                          derived,
                          variant == 8 ? operations[op].opposite : operations[op].sign,
                          variant == 6 ? get_null_object() : handler,
                          variant == 7 ? ACCESS_PRIVATE : ACCESS_EXTERNAL);
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
                int status = run(p, f.code);
                if (variant == 5) {
                    assert(failed_rhs_calls == 1);
                    /* A successfully selected own handler never consults the ancestor. */
                    assert(status == 0 && !t->exception.value && failed_scope_handler_calls == 1);
                } else {
                    assert(status != 0 && failed_scope_handler_calls == 0 && failed_rhs_calls == 0);
                    if (variant == 6 || variant == 7) {
                        /* Neither an invalid nor inaccessible own handler falls through. */
                        assert(t->exception.value
                               == (variant == 6 ? get_exception_invalid_operation()
                                                : get_exception_access_denied()));
                        assert(t->exception.trace && t->exception.trace != original.trace);
                    } else {
                        /* Missing handlers on a failed scope cannot select short-circuit fallback.
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
        dispose(&f);
    }
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    group = argv[1];
    if (!strcmp(group, "matrix"))
        matrix_case();
    else if (!strcmp(group, "fallback"))
        fallback_case();
    else if (!strcmp(group, "lookup"))
        lookup_case();
    else if (!strcmp(group, "access"))
        access_case();
    else if (!strcmp(group, "order"))
        order_case();
    else if (!strcmp(group, "invalid"))
        invalid_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "lifetime"))
        lifetime_case();
    else if (!strcmp(group, "builtin"))
        builtin_case();
    else if (!strcmp(group, "failed-file"))
        failed_file_case();
    else if (!strcmp(group, "wire"))
        wire_case();
    else
        assert(!"unknown logical test case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
