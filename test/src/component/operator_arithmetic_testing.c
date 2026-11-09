/** @file operator_arithmetic_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-VM arithmetic dispatch contracts (#496), also active in Release.
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

static const operator_case_t operators[] = {{U"+", U"a + rhs", ADD, 1},
                                            {U"-", U"a - rhs", SUB, 1},
                                            {U"*", U"a * rhs", MUL, 1},
                                            {U"/", U"a / rhs", DIVIDE, 1},
                                            {U"%", U"a % rhs", MODULO, 1},
                                            {U"**", U"a ** rhs", POWER, 1},
                                            {U"&", U"a & rhs", BAND, 1},
                                            {U"|", U"a | rhs", BOR, 1},
                                            {U"^", U"a ^ rhs", BXOR, 1},
                                            {U"<<", U"a << rhs", SHL, 1},
                                            {U">>", U"a >> rhs", SHR, 1},
                                            {U"+", U"+a", UPLUS, 0},
                                            {U"-", U"-a", UMINUS, 0},
                                            {U"~", U"~a", BNOT, 0},
                                            {U"++", U"++a", INC, 0},
                                            {U"--", U"--a", DEC, 0}};

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
    INCREF(receiver);
    return operation_success(receiver);
}

static operation_result_t fail_builtin(object_t **args, uint16_t count, thread_t *t) {
    (void)t;
    assert(count == 1);
    INCREF(args[0]);
    return operation_exception(args[0]);
}

static void setup(process_t *p) {
    static builtin_function_object_t gc_storage, probe_storage, fail_storage;
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
    field(p, restricted, U"+", get_function_sign(), ACCESS_PRIVATE);
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
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            const operator_case_t *op = &operators[i];
            string_value_t source = format_string(
                U"var result=false; const proto={}; var a=%s; const original=a; "
                U"const rhs={}; const state={};state.calls=0; %s[\"%s\"]=func(other){"
                U"if(this!=original || arguments.length!=%u) throw 101; "
                U"if(%u==1 && (other!=rhs || arguments[0]!=rhs)) throw 102; "
                U"state.calls+=1; return 42;}; const value=%s; result=value==42 && state.calls==1;",
                inherited ? U"proto -> {}" : U"{}",
                inherited ? U"proto" : U"a",
                op->sign,
                op->arguments,
                op->arguments,
                op->expression);
            program(source.data, op->opcode, false);
            FREE_STRING(source);
        }
}

static void arity_case(void) {
    program(U"var result=false; const a={}; a[\"-\"]=func(x){x=99;return arguments;}; "
            U"const unary=-a; const binary=a-null; collect(); "
            U"result=unary.length==0 && binary.length==1 && binary[0]==null;",
            UMINUS,
            false);
    program(U"var result=false; const a={}; a[\"+\"]=func(){return arguments;}; "
            U"const unary=+a; const binary=a+null; "
            U"result=unary.length==0 && binary.length==1 && binary[0]==null;",
            UPLUS,
            false);
    program(U"var result=false; const a={}; a[\"~\"]=func(missing){"
            U"return missing==null && arguments.length==0;}; result=~a;",
            BNOT,
            false);
}

static void values_case(void) {
    const gchar_t *values[] = {U"null",
                               U"true",
                               U"false",
                               U"47",
                               U"2.5",
                               U"\"text\"",
                               U"[1,2]",
                               U"{}",
                               U"func(){return 9;}"};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        string_value_t source =
            format_string(U"var result=false; const expected=%s; const a={}; "
                          U"a[\"*\"]=func(other){return expected;}; a[\"~\"]=a[\"*\"]; "
                          U"const first=a*3; const second=~a; collect(); "
                          U"result=first==expected && second==expected;",
                          values[i]);
        program(source.data, MUL, false);
        FREE_STRING(source);
    }
    program(U"var result=false; var log=0; const a={}; "
            U"a[\"+\"]=func(x){log+=1;this.changed=x;}; a+7; "
            U"result=log==1 && a.changed==7 && (a+8)==null && log==2;",
            ADD,
            false);
}

static void order_case(void) {
    program(U"var result=false; var log=0; var a={}; const original=a; "
            U"a[\"+\"]=func(x){throw 1;}; const left=func(){log=log*10+1;return a;}; "
            U"const right=func(){log=log*10+2;original[\"+\"]=func(x){"
            U"log=log*10+3;return this==original && x==7;}; a={}; return 7;}; "
            U"const value=left()+right(); result=value && log==123 && a!=original;",
            ADD,
            false);
    program(U"var result=false; const proto={}; const a=proto -> {}; "
            U"proto[\"*\"]=func(x){return 1;}; const right=func(){"
            U"proto[\"*\"]=func(x){return this==a && x==9;};return 9;}; result=a*right();",
            MUL,
            false);
    program(U"var result=false; var effects=0; const a={}; "
            U"a[\"-\"]=func(x){effects=99;}; const fail=func(){throw 7;}; "
            U"try {a-fail();} catch(e){result=e==7 && effects==0;}",
            SUB,
            false);
    program(U"var result=false; var effects=0; const left=func(){throw 8;}; "
            U"const right=func(){effects+=1;return 1;}; "
            U"try {left()+right();} catch(e){result=e==8 && effects==0;}",
            ADD,
            false);
    program(U"var result=false; var effects=0; const a={}; "
            U"const right=func(){effects+=1;return 1;}; "
            U"try {a/right();} catch(e){result=e==Exceptions.INVALID_OPERATION && effects==1;}",
            DIVIDE,
            false);
}

static void invalid_case(void) {
    const gchar_t *bad[] = {U"null", U"3", U"false", U"\"x\"", U"[]", U"callable"};
    for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); ++i) {
        const operator_case_t *op = &operators[i];
        for (size_t j = 0; j <= sizeof(bad) / sizeof(*bad); ++j) {
            string_value_t initialization =
                j == 0 ? format_string(U" ")
                       : format_string(U"proto[\"%s\"]=func(){throw 999;}; a[\"%s\"]=%s;",
                                       op->sign,
                                       op->sign,
                                       bad[j - 1]);
            string_value_t source =
                format_string(U"var result=false; const proto={}; var a=proto -> {}; const rhs=1; "
                              U"const callable={}; callable[\"()\"]=func(){throw 998;}; %s "
                              U"try {%s;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
                              initialization.data,
                              op->expression);
            program(source.data, op->opcode, false);
            FREE_STRING(source);
            FREE_STRING(initialization);
        }
    }
    program(U"var result=false; var called=0; const a={}; "
            U"a[\"+\"]=func(x){called+=1;return 9;}; "
            U"try {3+a;} catch(e){result=e==Exceptions.INVALID_ARGUMENT && called==0;}",
            ADD,
            false);
    program(U"var result=false; const a={}; a[\"+\"]=func(x){return 99;}; "
            U"try {a-1;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            SUB,
            false);
}

static void access_case(void) {
    program(U"var result=false; try {restricted+1;} "
            U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            ADD,
            false);
    program(U"var result=false; const a=restricted -> {}; try {a+1;} "
            U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            ADD,
            false);
    program(U"var result=false; const a={private secret=3;}; "
            U"a[\"+\"]=func(x){return this.secret;}; try {a+1;} "
            U"catch(e){result=e==Exceptions.ACCESS_DENIED;}",
            ADD,
            false);
    program(U"var result=false; const outer={private secret=41; "
            U"external handler=func(x){return secret+x;};}; const a={}; "
            U"a[\"+\"]=outer.handler; result=(a+1)==42;",
            ADD,
            false);
}

static void updates_case(void) {
    program(U"var result=false; var a={}; const old=a; a[\"+\"]=func(x){throw 1;}; "
            U"a[\"++\"]=func(){return arguments.length==0 && this==old;}; "
            U"const value=++a; result=value==true && a==true;",
            INC,
            false);
    program(U"var result=false; var a={}; const old=a; a[\"-\"]=func(x){throw 1;}; "
            U"a[\"--\"]=func(){return arguments.length==0 && this==old;}; "
            U"const value=--a; result=value==true && a==true;",
            DEC,
            false);
    program(U"var result=false; var a={}; const old=a; a[\"++\"]=func(){"
            U"this.changed=7;return 9;}; const value=a++; "
            U"result=value==old && value.changed==7 && a==9;",
            INC,
            false);
    program(U"var result=false; var a={}; const old=a; a[\"--\"]=func(){return null;}; "
            U"const value=a--; result=value==old && a==null;",
            DEC,
            false);
    program(U"var result=false; var a={}; const old=a; a[\"++\"]=func(){throw 7;}; "
            U"try {++a;} catch(e){result=e==7 && a==old;}",
            INC,
            false);
    program(U"var result=false; var a={}; a[\"+\"]=func(x){return 99;}; "
            U"try {++a;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            INC,
            false);
    program(U"var result=false; var a={}; a[\"-\"]=func(x){return 99;}; "
            U"try {--a;} catch(e){result=e==Exceptions.INVALID_OPERATION;}",
            DEC,
            false);
    /* Simple variable writeback already exists; complex targets belong to #500. */
    program(U"var result=false; var a={}; a[\"+\"]=func(x){return x+5;}; "
            U"a[\"+=\"]=func(x){throw 1;}; a+=3; result=a==8;",
            ADD,
            false);
}

static void continuation_case(void) {
    program(U"var result=false; const a={}; const b={}; "
            U"a[\"+\"]=func(x){return 10+(b*x);}; b[\"*\"]=func(x){return x*2;}; "
            U"result=100+(a+3)*4==164;",
            ADD,
            false);
    program(U"var result=false; var log=0; const a={}; const b={}; "
            U"b[\"~\"]=func(){log=log*10+2;return 8;}; "
            U"a[\"+\"]=func(x){try {log=1;return x;} finally {~b; log=log*10+3;}}; "
            U"result=(a+7)==7 && log==123;",
            ADD,
            false);
    program(U"var result=false; const a={}; a[\"+\"]=func(x){"
            U"try {return x;} finally {return \"replacement\";}}; "
            U"result=(a+7)==\"replacement\";",
            ADD,
            false);
    program(U"var result=false; var log=0; const thrown={}; const a={}; const b={}; "
            U"b[\"~\"]=func(){throw thrown;}; a[\"+\"]=func(x){"
            U"try {return ~b;} finally {log=1;}}; "
            U"try {100+(a+3);} catch(e){result=e==thrown && log==1;} collect();",
            ADD,
            false);
    program(U"var result=false; const a={}; a[\"-\"]=func(x){try {throw 4;} "
            U"finally {throw 5;}}; try {a-1;} catch(e){result=e==5;}",
            SUB,
            false);
    program(U"const a={}; a[\"+\"]=func(x){const nested=func(){throw 73;}; "
            U"return nested();}; a+1;",
            ADD,
            true);
}

static void cleanup_case(void) {
    for (unsigned nonfunction = 0; nonfunction < 2; ++nonfunction) {
        string_value_t source = format_string(
            U"var result=false; var log=0; const a={}; %s "
            U"const rhs=func(){const temporary={};temporary.self=temporary;return temporary;}; "
            U"const inner=func(){try {return a+rhs();} finally {log=1;collect();}}; "
            U"const outer=func(first,second){throw 999;}; "
            U"try {outer(97,inner());} catch(e){result=e==Exceptions.INVALID_OPERATION && log==1;} "
            U"collect();",
            nonfunction ? U"a[\"+\"]=null;" : U"");
        program(source.data, ADD, false);
        FREE_STRING(source);
    }
    program(U"var result=false; const a={}; const inner=func(){"
            U"try {return a+{};} finally {collect();return 7;}}; "
            U"const outer=func(first,second){return first+second;}; "
            U"result=outer(97,inner())==104;",
            ADD,
            false);
    program(U"const a={}; const inner=func(){return a+{};}; "
            U"const outer=func(first,second){return first+second;}; outer(97,inner());",
            ADD,
            2);
}

static void builtin_case(void) {
    program(U"var result=false; const a={}; a[\"+\"]=sign; result=(a+(-6))==-1;", ADD, false);
    program(U"var result=false; const a={}; a[\"+\"]=sign; "
            U"try {+a;} catch(e){result=e==Exceptions.INVALID_ARGUMENT;}",
            UPLUS,
            false);
    program(U"var result=false; const a={}; a[\"+\"]=sign; "
            U"try {a+{};} catch(e){result=e==Exceptions.INVALID_ARGUMENT;}",
            ADD,
            false);
    program(U"var result=false; const a={}; const payload={}; a[\"+\"]=failBuiltin; "
            U"try {a+payload;} catch(e){result=e==payload;}",
            ADD,
            false);
    program(U"var result=false; const left=func(){const a={}; a.marker=81; "
            U"a[\"+\"]=probe;return a;}; const right=func(){const a={};a.marker=82;return a;}; "
            U"const value=left()+right(); result=value.marker==81;",
            ADD,
            false);
    program(U"var result=false; const make=func(){const a={}; a.marker=81; "
            U"a[\"~\"]=probe;return a;}; result=(~make()).marker==81;",
            BNOT,
            false);
}

static void lifetime_case(void) {
    program(U"var result=false; const make=func(){const a={}; a.marker=81; "
            U"a[\"+\"]=func(x){const self=this; this[\"+\"]=null; collect(); "
            U"const nested=func(){collect();return self.marker+x.marker;}; "
            U"try {return nested();} finally {collect();}}; return a;}; "
            U"const right=func(){collect();const a={};a.marker=82;return a;}; "
            U"result=10+(make()+right())==173;",
            ADD,
            false);
    program(U"var result=false; const make=func(){const a={}; a.marker=19; "
            U"a[\"~\"]=func(){const self=this; return func(){return self.marker;};}; return a;}; "
            U"const escaped=~make(); collect(); result=escaped()==19;",
            BNOT,
            false);
    program(U"var result=false; const a={}; a[\"+\"]=func(x){x=null;collect();return arguments;}; "
            U"const make=func(){const obj={};obj.marker=21;return obj;}; "
            U"const escaped=a+make(); collect(); result=escaped[0].marker==21;",
            ADD,
            false);
    program(U"var result=false; var count=0; const a={}; a[\"+\"]=func(x){"
            U"count+=1;collect();if(x==0) return 0; return this+(x-1);}; "
            U"result=(a+40)==0 && count==41;",
            ADD,
            false);
}

static void builtin_regression_case(void) {
    program(U"var result=2+3*4==14 && 17-3==14 && 15/3==5 && 17%3==2 && 2**5==32 "
            U"&& (12&10)==8 && (12|3)==15 && (12^10)==6 && (3<<2)==12 && (12>>2)==3 "
            U"&& +4==4 && -4==-4 && ~5==-6 && 1.5+2.5==4 && \"a\"+\"b\"==\"ab\";",
            NOP,
            false);
    program(U"var result=false; var x=2; const first=x++; const second=++x; "
            U"const third=x--; const fourth=--x; result=first==2 && second==4 && third==4 "
            U"&& fourth==2 && x==2;",
            INC,
            false);
    program(U"var result=false; var calls=0; const a={}; const handler=func(x){calls+=1;return "
            U"false;}; "
            U"a[\"==\"]=handler; a[\"!\"]=handler; a[\"&&\"]=handler; a[\"||\"]=handler; "
            U"a[\"[]\"]=handler; a[\"[]=\"]=handler; a[\"()\"]=handler; "
            U"a[\"x\"]=7; const same=a==a; const truth=!!a; const negation=!a; "
            U"false&&++calls; true||++calls; const value=a[\"x\"]; "
            U"result=same && truth && !negation && value==7 && calls==0;",
            NOP,
            false);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    group = argv[1];
    if (!strcmp(group, "matrix"))
        matrix_case();
    else if (!strcmp(group, "arity"))
        arity_case();
    else if (!strcmp(group, "values"))
        values_case();
    else if (!strcmp(group, "order"))
        order_case();
    else if (!strcmp(group, "invalid"))
        invalid_case();
    else if (!strcmp(group, "access"))
        access_case();
    else if (!strcmp(group, "updates"))
        updates_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "cleanup"))
        cleanup_case();
    else if (!strcmp(group, "builtin"))
        builtin_case();
    else if (!strcmp(group, "lifetime"))
        lifetime_case();
    else if (!strcmp(group, "builtin-regression"))
        builtin_regression_case();
    else
        assert(!"unknown arithmetic test case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
