/** @file operator_update_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Single-evaluation read-modify-write contracts (#500), also active in Release.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/abstract_state.h"
#include "analysis/analysis.h"
#include "analysis/module_interface.h"
#include "analysis/reachability.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "common/compilation_error.h"
#include "common/module_compilation.h"
#include "graph/compound_assignment.h"
#include "graph/index_access.h"
#include "graph/member_access.h"
#include "graph/update_expression.h"
#include "graph/variable.h"
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

static operation_result_t replace_setter(object_t **args, uint16_t count, thread_t *t) {
    assert(count == 2);
    object_t *k = key(t->process, U"[]=");
    operation_result_t result =
        write_accessible_property_result(&t->context->access, args[0], k, args[1]);
    DECREF(k);
    return result;
}

static operation_result_t three(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    return operation_success(get_static_integer_object(3));
}

static void setup(process_t *p) {
    static builtin_function_object_t gc_storage, replace_storage;
    static const builtin_function_t gc_descriptor = {.name = U"collect", .execute = collect};
    static const builtin_function_t replace_descriptor = {.name = U"replaceSetter",
                                                          .execute = replace_setter};
    field(p,
          p->main_thread->context->data,
          U"collect",
          get_builtin_function_object(&gc_storage, &gc_descriptor),
          ACCESS_EXTERNAL);
    field(p,
          p->main_thread->context->data,
          U"replaceSetter",
          get_builtin_function_object(&replace_storage, &replace_descriptor),
          ACCESS_EXTERNAL);
    object_t *root = get_root_object();
    object_t *restricted = create_user_defined_object(p, (object_array_t){&root, 1});
    const gchar_t *signs[] = {U"[]", U"[]=", U"+", U"++", U"--"};
    for (size_t i = 0; i < sizeof(signs) / sizeof(*signs); ++i)
        field(p, restricted, signs[i], get_function_sign(), ACCESS_PRIVATE);
    field(p, p->main_thread->context->data, U"restricted", restricted, ACCESS_EXTERNAL);
    DECREF(restricted);
    static builtin_function_object_t getter_storage;
    static const builtin_function_t getter_descriptor = {.name = U"three", .execute = three};
    object_t *setter_only = create_user_defined_object(p, (object_array_t){&root, 1});
    field(p,
          setter_only,
          U"[]",
          get_builtin_function_object(&getter_storage, &getter_descriptor),
          ACCESS_EXTERNAL);
    field(p, setter_only, U"[]=", get_function_sign(), ACCESS_PRIVATE);
    field(p, p->main_thread->context->data, U"restrictedSetter", setter_only, ACCESS_EXTERNAL);
    DECREF(setter_only);
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

/* All binary compound signs dispatch through the ordinary operator of the read
 * value. Neither the container nor a dedicated compound property participates. */
static void matrix_case(void) {
    const gchar_t *signs[] = {U"+", U"-", U"*", U"/", U"%", U"**", U"&", U"|", U"^", U"<<", U">>"};
    const gchar_t *targets[] = {U"a", U"box.item", U"box[11]", U"box[]", U"box[11,22,33]"};
    for (size_t sign = 0; sign < sizeof(signs) / sizeof(*signs); ++sign)
        for (size_t target = 0; target < sizeof(targets) / sizeof(*targets); ++target) {
            string_value_t s = format_string(
                U"var result=false;const old={};const next={};var calls=0;var a=old;"
                U"const box={};box.item=old;"
                U"old[\"%s\"]=func(rhs){if(this!=old || rhs!=7 || arguments.length!=1)throw 91;"
                U"calls+=1;return next;};old[\"%s=\"]=func(){throw 92;};"
                U"old[\"=\"]=func(){throw 93;};next[\"=\"]=func(){throw 94;};"
                U"box[\"%s\"]=func(){throw 95;};"
                U"box[\"[]\"]=func(){return this.item;};"
                U"box[\"[]=\"]=func(){this.item=arguments[arguments.length-1];return 999;};"
                U"var chain=null;const value=chain=(%s %s=7);"
                U"result=value==next && chain==next && %s==next && calls==1;",
                signs[sign],
                signs[sign],
                signs[sign],
                targets[target],
                signs[sign],
                target ? U"box.item" : U"a");
            program(s.data, NOP, 0);
            FREE_STRING(s);
        }
}

static void order_case(void) {
    program(U"var result=false;var log=0;const old={};var box=old;const value={};"
            U"const next={};var rhsValue=0;const other={};"
            U"old[\"[]\"]=func(x,y){log=log*10+4;rhsValue=7;"
            U"if(this!=old || x!=11 || y!=22)throw 91;return value;};"
            U"value[\"+\"]=func(rhs){log=log*10+6;if(rhs!=7)throw 92;return next;};"
            U"old[\"[]=\"]=func(x,y,v){log=log*10+7;"
            U"if(this!=old || x!=11 || y!=22 || v!=next)throw 93;return 999;};"
            U"const receiver=func(){log=log*10+1;return box;};"
            U"const first=func(){log=log*10+2;return 11;};"
            U"const second=func(){log=log*10+3;return 22;};"
            U"const rhs=func(){log=log*10+5;box=other;return rhsValue;};"
            U"var chain=null;chain=receiver()[first(),second()]+=rhs();"
            U"result=log==1234567 && chain==next && box==other;",
            SETINDEX,
            0);
    /* Dot access bypasses index overloads, including nested and call targets. */
    program(U"var result=false;var calls=0;const box={};const inner={};box.child=inner;"
            U"inner.item=7;inner[\"[]\"]=func(){throw 91;};"
            U"inner[\"[]=\"]=func(){throw 92;};"
            U"const receiver=func(){calls+=1;return box;};"
            U"const value=receiver().child.item+=3;"
            U"result=calls==1 && value==10 && inner.item==10;",
            ADD,
            0);
    program(U"var result=false;var log=0;const inner={};inner.item=4;const outer={};"
            U"outer[\"[]\"]=func(){log=log*10+2;return inner;};"
            U"const receiver=func(){log=log*10+1;return outer;};"
            U"const rhs=func(){log=log*10+3;return 6;};"
            U"const value=receiver()[].item+=rhs();"
            U"result=value==10 && inner.item==10 && log==123;",
            ADD,
            0);
    program(U"var result=false;var log=0;const box={};box.item=4;const outer={};"
            U"outer.child=box;const receiver=func(){log=log*10+1;return outer;};"
            U"const index=func(){log=log*10+2;return \"item\";};"
            U"const value=receiver().child[index()]+=6;"
            U"result=value==10 && box.item==10 && log==12;",
            ADD,
            0);
}

static void mutation_case(void) {
    for (unsigned in_operator = 0; in_operator < 2; ++in_operator)
        for (unsigned initially_bad = 0; initially_bad < 2; ++initially_bad) {
            string_value_t s =
                format_string(U"var result=false;var log=0;const box={};const old={};const next={};"
                              U"box[\"[]\"]=func(){log=1;return old;};box[\"[]=\"]=%s;"
                              U"const replacement=func(k,v){log=log*10+4;"
                              U"if(this!=box || k!=11 || v!=next)throw 91;return 999;};"
                              U"old[\"+\"]=func(rhs){log=log*10+3;%s return next;};"
                              U"const rhs=func(){log=log*10+2;%s return 7;};"
                              U"const value=box[11]+=rhs();result=value==next && log==1234;",
                              initially_bad ? U"null" : U"func(){throw 92;}",
                              in_operator ? U"replaceSetter(box,replacement);" : U"",
                              in_operator ? U"" : U"replaceSetter(box,replacement);");
            program(s.data, SETINDEX, 0);
            FREE_STRING(s);
        }
    /* A setter need not even exist until the arithmetic handler installs it. */
    program(U"var result=false;const box={};const old={};var stored=0;"
            U"box[\"[]\"]=func(){return old;};"
            U"old[\"+\"]=func(rhs){replaceSetter(box,func(v){stored=v;return 99;});return rhs;};"
            U"const value=box[]+=7;result=value==7 && stored==7;",
            SETINDEX,
            0);
    /* Getter-side replacement is also permitted before RHS and arithmetic. */
    program(U"var result=false;var stored=0;const box={};"
            U"box[\"[]\"]=func(){replaceSetter(box,func(v){stored=v;return 999;});return 3;};"
            U"box[\"[]=\"]=null;const value=box[]+=7;result=value==10 && stored==10;",
            SETINDEX,
            0);
    /* Updates perform late setter selection after their unary handler. */
    for (unsigned decrement = 0; decrement < 2; ++decrement) {
        string_value_t s = format_string(
            U"var result=false;const old={};const next={};var stored=null;const box={};"
            U"box[\"[]\"]=func(){return old;};box[\"[]=\"]=null;"
            U"old[\"%s\"]=func(){replaceSetter(box,func(v){stored=v;return 999;});return next;};"
            U"const value=box[]%s;result=value==old && stored==next;",
            decrement ? U"--" : U"++",
            decrement ? U"--" : U"++");
        program(s.data, decrement ? DEC : INC, 0);
        FREE_STRING(s);
    }
    /* Ordinary operator selection is also late, but the old read value is retained. */
    program(U"var result=false;const old={};var value=old;"
            U"old[\"+\"]=func(){throw 91;};const rhs=func(){value={};"
            U"old[\"+\"]=func(x){return x+10;};return 7;};"
            U"const assigned=value+=rhs();result=value==17 && assigned==17;",
            ADD,
            0);
}

static void updates_case(void) {
    const gchar_t *targets[] = {U"value",
                                U"receiver().item",
                                U"receiver()[index()]",
                                U"receiver()[]",
                                U"receiver()[index(),index()]"};
    for (unsigned decrement = 0; decrement < 2; ++decrement)
        for (unsigned postfix = 0; postfix < 2; ++postfix)
            for (size_t target = 0; target < sizeof(targets) / sizeof(*targets); ++target) {
                const gchar_t *sign = decrement ? U"--" : U"++";
                string_value_t s = format_string(
                    U"var result=false;var receivers=0;var indices=0;var operations=0;"
                    U"const old={};old.marker=0;const next={};next.marker=9;var value=old;"
                    U"const box={};box.item=old;"
                    U"old[\"%s\"]=func(){if(this!=old || arguments.length!=0)throw 91;"
                    U"operations+=1;this.marker=7;collect();return next;};"
                    U"box[\"[]\"]=func(){return this.item;};"
                    U"box[\"[]=\"]=func(){this.item=arguments[arguments.length-1];return {};};"
                    U"const receiver=func(){receivers+=1;return box;};"
                    U"const index=func(){indices+=1;return 11;};"
                    U"const observed=%s%s%s;collect();"
                    U"result=observed==%s && observed.marker==%u && %s==next && operations==1"
                    U" && receivers==%u && indices==%u;",
                    sign,
                    postfix ? U"" : sign,
                    targets[target],
                    postfix ? sign : U"",
                    postfix ? U"old" : U"next",
                    postfix ? 7 : 9,
                    target ? U"box.item" : U"value",
                    target ? 1 : 0,
                    target == 2   ? 1
                    : target == 4 ? 2
                                  : 0);
                program(s.data, decrement ? DEC : INC, 0);
                FREE_STRING(s);
            }
}

static void early_case(void) {
    const gchar_t *expressions[] = {U"value+=rhs()",
                                    U"++value",
                                    U"value++",
                                    U"--value",
                                    U"value--"};
    for (size_t i = 0; i < sizeof(expressions) / sizeof(*expressions); ++i) {
        string_value_t s = format_string(
            U"var result=false;var calls=0;const value={};"
            U"const handler=func(){calls+=1;return {};};"
            U"value[\"+\"]=handler;value[\"++\"]=handler;value[\"--\"]=handler;"
            U"const rhs=func(){calls+=10;return 1;};"
            U"try {%s;}catch(e){result=e==Exceptions.PROPERTY_IS_CONSTANT && calls==0;}",
            expressions[i]);
        program(s.data, CHECKVAR, 0);
        FREE_STRING(s);
    }
    const gchar_t *getters[] = {U"func(){log=2;throw 73;}", U"null"};
    for (size_t i = 0; i < 2; ++i) {
        string_value_t s =
            format_string(U"var result=false;var log=0;const box={};box[\"[]\"]=%s;"
                          U"const index=func(){log=1;return 11;};"
                          U"const rhs=func(){log=9;return 1;};"
                          U"try {box[index()]+=rhs();}catch(e){result=e==%s && log==%u;}",
                          getters[i],
                          i ? U"Exceptions.INVALID_OPERATION" : U"73",
                          i ? 1 : 2);
        program(s.data, GETINDEX, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;var log=0;const fail=func(){log=1;throw 73;};"
            U"const later=func(){log=9;return 0;};"
            U"try {fail()[later()]+=later();}catch(e){result=e==73 && log==1;}",
            SETINDEX,
            0);
    program(U"var result=false;var log=0;const box={};"
            U"const fail=func(){log=1;throw 73;};const later=func(){log=9;return 0;};"
            U"try {box[fail(),later()]+=later();}catch(e){result=e==73 && log==1;}",
            SETINDEX,
            0);
    /* A const binding does not make the referenced object's fields constant. */
    program(U"var result=false;const box={};box.item=7;box.item+=3;"
            U"const value=++box.item;result=value==11 && box.item==11;",
            INC,
            0);
}

static void invalid_case(void) {
    const gchar_t *bad[] = {U"null", U"false", U"3", U"\"x\"", U"[]", U"callable"};
    for (unsigned setter = 0; setter < 2; ++setter)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
                string_value_t s = format_string(
                    U"var result=false;const state={};state.log=0;const proto={};const "
                    U"box=%s;const old={};"
                    U"const callable={};callable[\"()\"]=func(){throw 91;};"
                    U"%s proto[\"%s\"]=%s;"
                    U"old[\"+\"]=func(rhs){state.log=state.log*10+3;return rhs;};"
                    U"const rhs=func(){state.log=state.log*10+2;return 7;};"
                    U"try {box[]+=rhs();}catch(e){result=e==Exceptions.INVALID_OPERATION && "
                    U"state.log==%u;}",
                    inherited ? U"proto -> {}" : U"proto",
                    setter ? U"proto[\"[]\"]=func(){state.log=1;return old;};" : U"",
                    setter ? U"[]=" : U"[]",
                    bad[i],
                    setter ? 123 : 0);
                program(s.data, SETINDEX, 0);
                FREE_STRING(s);
            }
    for (unsigned inherited = 0; inherited < 2; ++inherited) {
        string_value_t s = format_string(
            U"var result=false;var calls=0;const box=%s;const rhs=func(){calls+=1;return 7;};"
            U"try {box[]+=rhs();}catch(e){result=e==Exceptions.ACCESS_DENIED && calls==0;}",
            inherited ? U"restricted -> {}" : U"restricted");
        program(s.data, GETINDEX, 0);
        FREE_STRING(s);
    }
    /* An own getter avoids the private inherited getter; setter access is late. */
    program(U"var result=false;var calls=0;const box=restrictedSetter -> {};"
            U"const rhs=func(){calls=1;return 7;};"
            U"try {box[]+=rhs();}catch(e){result=e==Exceptions.ACCESS_DENIED && calls==1;}",
            SETINDEX,
            0);
    for (unsigned decrement = 0; decrement < 2; ++decrement)
        for (unsigned nonfunction = 0; nonfunction < 2; ++nonfunction) {
            string_value_t s = format_string(
                U"var result=false;var calls=0;const old={};var value=old;"
                U"old[\"%s\"]=func(x){calls+=1;return x;};%s"
                U"try {%svalue;}catch(e){result=e==Exceptions.INVALID_OPERATION && calls==0 && "
                U"value==old;}",
                decrement ? U"-" : U"+",
                nonfunction ? (decrement ? U"old[\"--\"]=null;" : U"old[\"++\"]=null;") : U"",
                decrement ? U"--" : U"++");
            program(s.data, decrement ? DEC : INC, 0);
            FREE_STRING(s);
        }
    program(U"var result=false;var value=restricted;try {++value;}"
            U"catch(e){result=e==Exceptions.ACCESS_DENIED && value==restricted;}",
            INC,
            0);
    /* Missing setters use the ordinary write path, including its arity error. */
    program(U"var result=false;var calls=0;const box={};box[\"[]\"]=func(){return 3;};"
            U"const rhs=func(){calls=1;return 7;};try {box[]+=rhs();}"
            U"catch(e){result=e==Exceptions.INVALID_ARGUMENT && calls==1;}",
            SETINDEX,
            0);
}

static void unwind_case(void) {
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        string_value_t s = format_string(
            U"var result=false;var log=0;const box={};const old={};old.marker=0;"
            U"box[\"[]\"]=func(){log=1;return old;};box[\"[]=\"]=%s;"
            U"old[\"+\"]=func(rhs){log=log*10+3;this.marker=7;collect();return {};};"
            U"const rhs=func(){log=log*10+2;return 1;};var assigned=99;"
            U"const inner=func(){try {assigned=box[]+=rhs();}finally {collect();log=log*10+5;}};"
            U"const outer=func(a,b){throw 91;};try {outer(81,inner());}"
            U"catch(e){result=e==%s && log==%u && old.marker==7 && assigned==99;}collect();",
            invalid ? U"null" : U"func(v){log=log*10+4;throw 73;}",
            invalid ? U"Exceptions.INVALID_OPERATION" : U"73",
            invalid ? 1235 : 12345);
        program(s.data, SETINDEX, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;var calls=0;const old={};old.marker=0;const box={};"
            U"box[\"[]\"]=func(){return old;};box[\"[]=\"]=func(){calls=9;};"
            U"old[\"++\"]=func(){this.marker=7;throw 73;};"
            U"try {box[]++;}catch(e){result=e==73 && old.marker==7 && calls==0;}",
            INC,
            0);
    program(U"var result=false;var calls=0;const box={};const old={};old.marker=0;"
            U"box[\"[]\"]=func(){return old;};box[\"[]=\"]=func(){calls=9;};"
            U"old[\"+\"]=func(){this.marker=7;throw 73;};"
            U"try {box[]+=1;}catch(e){result=e==73 && old.marker==7 && calls==0;}",
            ADD,
            0);
    program(U"var result=false;var calls=0;const box={};const old={};"
            U"box[\"[]\"]=func(){calls=1;return old;};"
            U"box[\"[]=\"]=func(){calls=9;};old[\"+\"]=func(){calls=8;};"
            U"const rhs=func(){calls=2;throw 73;};"
            U"try {box[]+=rhs();}catch(e){result=e==73 && calls==2;}",
            ADD,
            0);
    program(U"const box={};box[\"[]\"]=func(){return 1;};"
            U"box[\"[]=\"]=func(){const nested=func(){throw 73;};nested();};"
            U"const outer=func(){box[]+=7;};outer();",
            SETINDEX,
            1);
}

static void continuation_case(void) {
    /* Nested blocks own their this; retain the setter receiver explicitly across try/finally. */
    program(U"var result=false;const old={};const box={};box.item=old;const other={};other.item=5;"
            U"box[\"[]\"]=func(){collect();return this.item;};"
            U"box[\"[]=\"]=func(k,v){const self=this;try {self.item=v;}finally "
            U"{other.item++;collect();}return 999;};"
            U"old[\"+\"]=func(rhs){try {return other.item+=rhs;}finally {collect();}};"
            U"const value=100+(box[11]+=7)*2;"
            U"result=value==124 && box.item==12 && other.item==13;",
            SETINDEX,
            0);
    program(U"var result=false;const box={};box.item=7;"
            U"box[\"[]\"]=func(){return this.item;};"
            U"box[\"[]=\"]=func(v){this.item=v;return null;};"
            U"const call=func(a,b,c){return a*100+b*10+c;};"
            U"const value=call(box[]++,++box[],box[]--);"
            U"result=value==799 && box.item==8;",
            INC,
            0);
    /* Both retained address and old/new objects survive collections in every stage. */
    program(
        U"var result=false;var saved=null;const make=func(){const box={};const old={};"
        U"old.marker=7;old[\"+\"]=func(rhs){collect();const next={};"
        U"next.marker=this.marker+rhs.marker;return next;};"
        U"box[\"[]\"]=func(k){collect();if(k.marker!=11)throw 91;return old;};"
        U"box[\"[]=\"]=func(k,v){collect();if(k.marker!=11)throw 92;saved=v;return {};};return "
        U"box;};"
        U"const index=func(){const key={};key.marker=11;return key;};"
        U"const rhs=func(){collect();const value={};value.marker=5;return value;};"
        U"const value=make()[index()]+=rhs();collect();result=value==saved && value.marker==12;",
        SETINDEX,
        0);
}

static void builtin_case(void) {
    program(
        U"var result=false;var a=10;a+=2;a-=3;a*=4;a/=2;a%=7;a|=8;a&=15;a^=3;a<<=1;a>>=1;"
        U"const box={};box.item=a;const values=[a];box.item+=2;values[0]+=3;"
        U"const first=values[0]++;const second=++box.item;var power=3;power**=3;"
        U"result=a==15 && first==18 && values[0]==19 && second==18 && box.item==18 && power==27;",
        INC,
        0);
    program(U"var result=false;var calls=0;const old={};const next={};"
            U"old[\"=\"]=func(){calls+=1;};next[\"=\"]=func(){calls+=10;};"
            U"var value=old;value=next;result=value==next && calls==0;",
            NOP,
            0);
    program(U"var result=false;const array=[1];var calls=0;const rhs=func(){calls+=1;return 7;};"
            U"try {array[]+=rhs();}catch(e){result=e==Exceptions.INVALID_ARGUMENT && calls==0;}",
            GETINDEX,
            0);
}

/* Const preflight must also precede RHS abstract effects, not just VM effects. */
static void analysis_case(void) {
    for (unsigned reachability = 0; reachability < 2; ++reachability) {
        arena_t *arena = create_arena(8);
        constant_declaration_pair_t constant = create_synthetic_constant_declaration_node(
            arena,
            (string_view_t){U"constant", 8},
            (expression_t *)create_integer_node(arena, 7));
        variable_declaration_pair_t side =
            create_synthetic_variable_declaration_node(arena, (string_view_t){U"side", 4});
        variable_t *left = (variable_t *)create_variable_node(arena, constant.declarator->name);
        left->declarator = constant.declarator;
        variable_t *right = (variable_t *)create_variable_node(arena, side.declarator->name);
        right->declarator = side.declarator;
        expression_t *effect =
            create_simple_assignment_node(arena,
                                          &right->base,
                                          (expression_t *)create_integer_node(arena, 9));
        expression_t *compound = create_compound_assignment_node(arena, &left->base, effect, U"+=");
        abstract_state_t *state = create_abstract_state(arena);
        set_in_abstract_state(state, constant.declarator, make_integer_constant_element(arena, 7));
        const lattice_element_t *original = make_integer_constant_element(arena, 1);
        set_in_abstract_state(state, side.declarator, original);
        const lattice_element_t *result =
            reachability ? visit_reachable_node((node_t *)compound, &state, NULL)
                         : calculate_expression(compound, state, arena);
        assert(result->type == LATTICE_BOTTOM && state->control_flow == FLOW_UNREACHABLE);
        assert(get_from_abstract_state(state, side.declarator) == original);
        destroy_abstract_state(state);
        destroy_arena(arena);
    }
    program(U"var result=false;const obj={};obj.items=[1];const array=[obj];"
            U"const receiver=func(){return array;};"
            U"const a=++((array[0].items[0]));const b=((receiver()[0]).items[0])++;"
            U"const c=(array[0].items[0])--;const d=--(array[0].items[0]);"
            U"result=a==2 && b==2 && c==3 && d==1 && obj.items[0]==1;",
            INC,
            0);
    /* Captured receiver/index side effects must survive call-effect analysis. */
    program(U"var result=false;var calls=0;const box=[7];"
            U"const receiver=func(){calls+=1;return box;};"
            U"const index=func(){calls+=10;return 0;};"
            U"const update=func(){return receiver()[index()]++;};"
            U"const value=update();result=value==7 && box[0]==8 && calls==11;",
            INC,
            0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    group = argv[1];
    if (!strcmp(group, "matrix"))
        matrix_case();
    else if (!strcmp(group, "order"))
        order_case();
    else if (!strcmp(group, "mutation"))
        mutation_case();
    else if (!strcmp(group, "updates"))
        updates_case();
    else if (!strcmp(group, "early"))
        early_case();
    else if (!strcmp(group, "invalid"))
        invalid_case();
    else if (!strcmp(group, "unwind"))
        unwind_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "builtin"))
        builtin_case();
    else if (!strcmp(group, "analysis"))
        analysis_case();
    else
        assert(!"unknown update test case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
