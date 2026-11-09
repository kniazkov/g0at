/** @file operator_comparison_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-VM comparison and negation contracts (#497), also active in Release.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/analysis.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "common/compilation_error.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/exception.h"
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

static const operator_case_t operators[] = {{U"==", U"a == rhs", EQUAL, 1},
                                            {U"!=", U"a != rhs", DIFF, 1},
                                            {U"<", U"a < rhs", LESS, 1},
                                            {U"<=", U"a <= rhs", LEQ, 1},
                                            {U">", U"a > rhs", GREATER, 1},
                                            {U">=", U"a >= rhs", GREQ, 1},
                                            {U"!", U"!a", LNOT, 0}};

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
    object_t *only_equal = create_user_defined_object(p, (object_array_t){&root, 1});
    object_t *only_different = create_user_defined_object(p, (object_array_t){&root, 1});
    field(p, only_equal, U"==", get_function_sign(), ACCESS_PRIVATE);
    field(p, only_different, U"!=", get_function_sign(), ACCESS_PRIVATE);
    field(p, p->main_thread->context->data, U"onlyEqual", only_equal, ACCESS_EXTERNAL);
    field(p, p->main_thread->context->data, U"onlyDifferent", only_different, ACCESS_EXTERNAL);
    DECREF(only_equal);
    DECREF(only_different);
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
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (unsigned truth = 0; truth < 2; ++truth) {
                const operator_case_t *op = &operators[i];
                string_value_t source = format_string(
                    U"var result=false; const proto={}; const a=%s; a.marker=81; "
                    U"const rhs={}; rhs.marker=82; const state={};state.calls=0; "
                    U"%s[\"%s\"]=func(other){"
                    U"if(this.marker!=81 || arguments.length!=%u) throw 101; "
                    U"if(%u==1 && (other.marker!=82 || arguments[0].marker!=82)) throw 102; "
                    U"state.calls+=1; return %s;}; const value=%s; result=value==%s && "
                    U"state.calls==1;",
                    inherited ? U"proto -> {}" : U"{}",
                    inherited ? U"proto" : U"a",
                    op->sign,
                    op->arguments,
                    op->arguments,
                    truth ? U"true" : U"false",
                    op->expression,
                    truth ? U"true" : U"false");
                program(source.data, op->opcode, false);
                FREE_STRING(source);
            }
}

static void fallback_case(void) {
    for (unsigned reverse = 0; reverse < 2; ++reverse)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (unsigned truth = 0; truth < 2; ++truth) {
                string_value_t source = format_string(
                    U"var result=false; const state={};state.calls=0; const proto={}; const a=%s; "
                    U"a.marker=81; "
                    U"%s[\"%s\"]=func(x){state.calls+=1; collect(); return this.marker==81 "
                    U"&& arguments.length==1 && x==7 && %s;}; const value=a %s 7; "
                    U"result=value==%s && state.calls==1;",
                    inherited ? U"proto -> {}" : U"{}",
                    inherited ? U"proto" : U"a",
                    reverse ? U"!=" : U"==",
                    truth ? U"true" : U"false",
                    reverse ? U"==" : U"!=",
                    truth ? U"false" : U"true");
                program(source.data, reverse ? EQUAL : DIFF, false);
                FREE_STRING(source);
            }
    program(U"var result=false; const a={}; const b={}; const proto={}; const c=proto -> {}; "
            U"result=a==a && !(a!=a) && a!=b && !(a==b) && c==c && c!=proto "
            U"&& a!=null && !(a==null) && a!=3 && !(a==3);",
            EQUAL,
            false);
    program(U"var result=false; const state={};state.calls=0; const a={}; "
            U"a[\"==\"]=func(x){state.calls+=1;return false;}; "
            U"a[\"!=\"]=func(x){state.calls+=10;return false;}; const eq=a==a; const ne=a!=a; "
            U"result=!eq && !ne && state.calls==11;",
            EQUAL,
            false);
    program(U"var result=false; const state={};state.calls=0; const a={}; "
            U"a[\"!=\"]=func(x){state.calls+=1;return true;}; "
            U"const eq=a==a; result=!eq && state.calls==1;",
            EQUAL,
            false);
    program(U"var result=false; const proto={}; proto[\"==\"]=func(){return true;}; "
            U"const a=proto -> {}; a[\"!=\"]=func(){throw 91;}; result=a==7;",
            EQUAL,
            false);
    program(U"var result=false; const proto={}; proto[\"!=\"]=func(){return true;}; "
            U"const a=proto -> {}; a[\"==\"]=func(){throw 91;}; result=a!=7;",
            DIFF,
            false);
}

static void invalid_case(void) {
    const gchar_t *values[] =
        {U"null", U"0", U"1", U"2.5", U"\"\"", U"\"text\"", U"[]", U"{}", U"func(){return true;}"};
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i) {
        const operator_case_t *op = &operators[i];
        for (size_t j = 0; j < sizeof(values) / sizeof(*values); ++j) {
            string_value_t source =
                format_string(U"var result=false; var calls=0; const a={}; const rhs=7; "
                              U"a[\"%s\"]=func(){calls+=1;return %s;}; try {%s;} "
                              U"catch(e){result=e==Exceptions.INVALID_OPERATION && calls==1;}",
                              op->sign,
                              values[j],
                              op->expression);
            program(source.data, op->opcode, false);
            FREE_STRING(source);
        }
        /* Falling off a handler also returns a non-boolean (null). */
        string_value_t source =
            format_string(U"var result=false; const a={}; const rhs=7; a[\"%s\"]=func(){}; "
                          U"try {%s;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                          op->sign,
                          op->expression);
        program(source.data, op->opcode, false);
        FREE_STRING(source);
    }
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        string_value_t source =
            format_string(U"var result=false; const a={}; a[\"%s\"]=func(){return 1;}; "
                          U"try {a %s a;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                          reverse ? U"!=" : U"==",
                          reverse ? U"==" : U"!=");
        program(source.data, reverse ? EQUAL : DIFF, false);
        FREE_STRING(source);
    }
}

static void lookup_case(void) {
    const gchar_t *bad[] = {U"null", U"3", U"false", U"\"x\"", U"[]", U"callable"};
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); ++j) {
                const operator_case_t *op = &operators[i];
                string_value_t source = format_string(
                    U"var result=false; var calls=0; const proto={}; const a=proto -> {}; const "
                    U"rhs=7; "
                    U"const callable={}; callable[\"()\"]=func(){throw 998;}; "
                    U"proto[\"%s\"]=func(){throw 999;}; %s[\"%s\"]=%s; %s "
                    U"try {%s;} catch(e){result=e==Exceptions.INVALID_OPERATION && calls==0;}",
                    op->sign,
                    inherited ? U"proto" : U"a",
                    op->sign,
                    bad[j],
                    i == 0   ? U"a[\"!=\"]=func(){calls+=1;return true;};"
                    : i == 1 ? U"a[\"==\"]=func(){calls+=1;return true;};"
                             : U"",
                    op->expression);
                program(source.data, op->opcode, false);
                FREE_STRING(source);
            }
    /* The opposite property is also subject to lookup errors, never identity fallback. */
    for (unsigned reverse = 0; reverse < 2; ++reverse)
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            string_value_t source =
                format_string(U"var result=false; const proto={}; const a=%s; %s[\"%s\"]=null; "
                              U"try {a %s a;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                              inherited ? U"proto -> {}" : U"{}",
                              inherited ? U"proto" : U"a",
                              reverse ? U"!=" : U"==",
                              reverse ? U"==" : U"!=");
            program(source.data, reverse ? EQUAL : DIFF, false);
            FREE_STRING(source);
        }
}

static void access_case(void) {
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i)
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            const operator_case_t *op = &operators[i];
            string_value_t source =
                format_string(U"var result=false; const a=%s; const rhs=7; try {%s;} "
                              U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
                              inherited ? U"restricted -> {}" : U"restricted",
                              op->expression);
            program(source.data, op->opcode, false);
            FREE_STRING(source);
        }
    program(U"var result=false; try {onlyEqual!=onlyEqual;} "
            U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            DIFF,
            false);
    program(U"var result=false; const a=onlyDifferent -> {}; try {a==a;} "
            U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            EQUAL,
            false);
    program(U"var result=false; const a=onlyEqual -> {}; a[\"!=\"]=func(){return true;}; "
            U"try {a==a;} catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            EQUAL,
            false);
    program(U"var result=false; const a=onlyDifferent -> {}; a[\"==\"]=func(){return true;}; "
            U"try {a!=a;} catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            DIFF,
            false);
    program(
        U"var result=false; const a={private secret=true;}; a[\"!\"]=func(){return this.secret;}; "
        U"try {!a;} catch(e){result=e==Exceptions.ACCESS_DENIED;}",
        LNOT,
        false);
}

static void independent_case(void) {
    for (size_t i = 2; i < 6; ++i) {
        const operator_case_t *op = &operators[i];
        string_value_t source =
            format_string(U"var result=false; const proto={}; const a=proto -> {}; const rhs=a; "
                          U"proto[\"==\"]=func(){throw 99;}; proto[\"!=\"]=func(){throw 99;}; "
                          U"proto[\"%s\"]=func(){throw 99;}; try {%s;} "
                          U"catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                          operators[i == 5 ? 2 : i + 1].sign,
                          op->expression);
        program(source.data, op->opcode, false);
        FREE_STRING(source);
    }
    program(U"var result=false; var calls=0; const a={}; a[\"==\"]=func(){calls+=1;return true;}; "
            U"a[\"!=\"]=a[\"==\"]; result=!(7==a) && 7!=a && calls==0;",
            EQUAL,
            false);
}

static void truth_case(void) {
    program(U"var result=false; var calls=0; var visits=0; const a={}; "
            U"a[\"!\"]=func(){calls+=1;return true;}; a[\"!!\"]=func(){throw 99;}; "
            U"if(a) visits+=1; while(a){visits+=1;break;} const truth=!!a; "
            U"const logical=(a&&true) && (a||false); "
            U"const before=calls; const twice=!(!a); "
            U"result=visits==2 && truth && logical && before==0 && calls==1 && !twice;",
            BOOL,
            false);
    program(U"var result=false; const a={}; const b={} -> {}; "
            U"result=!a && !b && !(!!a) && !(!!b);",
            LNOT,
            false);
    program(U"var result=false; const a={}; a[\"!\"]=null; "
            U"if(a) result=!!a; while(a){break;}",
            BOOL,
            false);
}

static void identity_case(void) {
    program(U"var result=false; var calls=0; const a={}; const b={}; const map={}; "
            U"a[\"==\"]=func(){calls+=1;throw 99;}; b[\"==\"]=a[\"==\"]; "
            U"map[a]=11; map[b]=22; const first=map[a]; const second=map[b]; "
            U"map[a]=33; result=first==11 && second==22 && map[a]==33 && map[b]==22 && calls==0;",
            NOP,
            false);
}

static void order_case(void) {
    program(U"var result=false; var log=0; var a={}; const original=a; "
            U"a[\"==\"]=func(x){throw 1;}; const left=func(){log=log*10+1;return a;}; "
            U"const right=func(){log=log*10+2;original[\"==\"]=func(x){"
            U"log=log*10+3;return this.marker==81 && x==7;}; original.marker=81;a={};return 7;}; "
            U"const value=left()==right(); result=value && log==123;",
            EQUAL,
            false);
    program(
        U"var result=false; var effects=0; const a={}; a[\"!=\"]=func(){effects=99;return true;}; "
        U"const fail=func(){throw 7;}; try {a==fail();} catch(e){result=e==7 && effects==0;}",
        EQUAL,
        false);
    program(U"var result=false; var effects=0; const left=func(){throw 8;}; "
            U"const right=func(){effects+=1;return 1;}; "
            U"try {left()<right();} catch(e){result=e==8 && effects==0;}",
            LESS,
            false);
    program(U"var result=false; const proto={}; const a=proto -> {}; "
            U"proto[\"!=\"]=func(){throw 99;}; const right=func(){"
            U"proto[\"!=\"]=func(x){return x!=null;};return null;}; result=a==right();",
            EQUAL,
            false);
    program(U"var result=false; const a={}; a[\"==\"]=func(x){"
            U"return arguments.length==1 && arguments[0]==null && x==null;}; result=a==null;",
            EQUAL,
            false);
}

static void continuation_case(void) {
    program(U"var result=false; var log=0; const a={}; const b={}; "
            U"b[\"!\"]=func(){log=log*10+2;collect();return false;}; "
            U"a[\"==\"]=func(x){try {log=1;return !b;} finally {log=log*10+3;collect();}}; "
            U"result=(a!=7) && log==123;",
            DIFF,
            false);
    program(U"var result=false; const a={}; a[\"!=\"]=func(){"
            U"try {return null;} finally {return true;}}; result=!(a==a);",
            EQUAL,
            false);
    program(U"var result=false; const a={}; a[\"==\"]=func(){"
            U"try {return true;} finally {return null;}}; "
            U"try {a!=a;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            DIFF,
            false);
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i) {
        const operator_case_t *op = &operators[i];
        string_value_t source = format_string(
            U"var result=false; var log=0; const thrown={}; const a={}; const rhs=7; "
            U"a[\"%s\"]=func(){try {log=1;throw thrown;} finally {log=2;collect();}}; "
            U"try {%s;} catch(e){result=e==thrown && log==2;}",
            op->sign,
            op->expression);
        program(source.data, op->opcode, false);
        FREE_STRING(source);
    }
    program(U"const a={}; a[\"==\"]=func(){const nested=func(){throw 73;};return nested();};a!=a;",
            DIFF,
            1);
    program(U"const a={}; const inner=func(){return a<{};}; "
            U"const outer=func(first,second){return first+second;}; outer(97,inner());",
            LESS,
            2);
}

static void lifetime_case(void) {
    program(U"var result=false; const make=func(){const a={}; a.marker=81; "
            U"a[\"==\"]=func(x){const self=this; this[\"==\"]=null; collect(); "
            U"const nested=func(){collect();return self.marker+x.marker==163;}; "
            U"try {return nested();} finally {collect();}}; return a;}; "
            U"const right=func(){collect();const a={};a.marker=82;return a;}; "
            U"result=!(make()!=right());",
            DIFF,
            false);
    program(U"var result=false; var count=0; const a={}; a[\"==\"]=func(x){"
            U"count+=1;collect();if(x==0) return true; return this==(x-1);}; "
            U"result=(a==40) && count==41;",
            EQUAL,
            false);
    program(
        U"var result=false; const make=func(){const a={};a.marker=81;a[\"<\"]=probe;return a;}; "
        U"const right=func(){const a={};a.marker=82;return a;}; result=make()<right();",
        LESS,
        false);
    program(
        U"var result=false; const make=func(){const a={};a.marker=81;a[\"!\"]=probe;return a;}; "
        U"result=!make();",
        LNOT,
        false);
    program(
        U"var result=false; const make=func(){const a={};a.marker=81;a[\"!=\"]=probe;return a;}; "
        U"const right=func(){const a={};a.marker=82;return a;}; result=!(make()==right());",
        EQUAL,
        false);
    program(U"var result=false; var log=0; const a={}; a[\"==\"]=func(){return null;}; "
            U"const inner=func(){try {return a=={};} finally {log=1;collect();}}; "
            U"const outer=func(first,second){throw 999;}; try {outer(97,inner());} "
            U"catch(e){result=e==Exceptions.INVALID_OPERATION && log==1;} collect();",
            EQUAL,
            false);
    program(U"var result=false; const a={}; a[\"<\"]=sign; "
            U"try {a<7;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            LESS,
            false);
    program(U"var result=false; const a={}; const payload={}; a[\"==\"]=failBuiltin; "
            U"try {a!=payload;} catch(e){result=e==payload;}",
            DIFF,
            false);
}

static void builtin_case(void) {
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i) {
        const operator_case_t *op = &operators[i];
        string_value_t source = format_string(
            U"var result=false; const a={}; const rhs=null; a[\"%s\"]=booleanBuiltin; "
            U"const value=%s; a[\"%s\"]=nonbooleanBuiltin; "
            U"try {%s;} catch(e){result=value && e==Exceptions.INVALID_OPERATION;}",
            op->sign,
            op->expression,
            op->sign,
            op->expression);
        program(source.data, op->opcode, false);
        FREE_STRING(source);
    }
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        string_value_t source = format_string(
            U"var result=false; const a={}; a[\"%s\"]=booleanBuiltin; "
            U"const value=a %s null; a[\"%s\"]=nonbooleanBuiltin; "
            U"try {a %s null;} catch(e){result=!value && e==Exceptions.INVALID_OPERATION;}",
            reverse ? U"!=" : U"==",
            reverse ? U"==" : U"!=",
            reverse ? U"!=" : U"==",
            reverse ? U"==" : U"!=");
        program(source.data, reverse ? EQUAL : DIFF, false);
        FREE_STRING(source);
    }
}

static void builtin_regression_case(void) {
    program(U"var result=3==3 && 3==3.0 && 3!=4 && 3<4 && 3<=3 && 4>3 && 4>=4 "
            U"&& true==true && true!=false && \"a\"==\"a\" && \"a\"!=\"b\" "
            U"&& null==null && null!=3 && true!=1 && !false && !null && !0 "
            U"&& !!1 && !!true && !(!\"text\");",
            NOP,
            false);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    group = argv[1];
    if (!strcmp(group, "matrix"))
        matrix_case();
    else if (!strcmp(group, "fallback"))
        fallback_case();
    else if (!strcmp(group, "invalid"))
        invalid_case();
    else if (!strcmp(group, "lookup"))
        lookup_case();
    else if (!strcmp(group, "access"))
        access_case();
    else if (!strcmp(group, "independent"))
        independent_case();
    else if (!strcmp(group, "truth"))
        truth_case();
    else if (!strcmp(group, "identity"))
        identity_case();
    else if (!strcmp(group, "order"))
        order_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "lifetime"))
        lifetime_case();
    else if (!strcmp(group, "builtin"))
        builtin_case();
    else if (!strcmp(group, "builtin-regression"))
        builtin_regression_case();
    else
        assert(!"unknown comparison test case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
