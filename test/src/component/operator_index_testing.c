/** @file operator_index_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-VM index handler contracts (#499), also active in Release.
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
#include "graph/index_access.h"
#include "graph/member_access.h"
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

static operation_result_t replace_setter(object_t **args, uint16_t count, thread_t *t);

static void setup(process_t *p) {
    static builtin_function_object_t replace_storage;
    static const builtin_function_t replace_descriptor = {.name = U"replaceSetter",
                                                          .execute = replace_setter};
    field(p,
          p->main_thread->context->data,
          U"replaceSetter",
          get_builtin_function_object(&replace_storage, &replace_descriptor),
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
    field(p, restricted, U"[]", get_function_sign(), ACCESS_PRIVATE);
    field(p, restricted, U"[]=", get_function_sign(), ACCESS_PRIVATE);
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

/* Model a host-side property mutation during operand evaluation. This is a
 * fixture builtin, not a language escape hatch around index dispatch. */
static operation_result_t replace_setter(object_t **args, uint16_t count, thread_t *t) {
    assert(count == 2);
    object_t *k = key(t->process, U"[]=");
    operation_result_t result =
        write_accessible_property_result(&t->context->access, args[0], k, args[1]);
    DECREF(k);
    return result;
}

static void matrix_case(void) {
    const gchar_t *indices[] = {U"", U"11", U"11,22,33"};
    unsigned counts[] = {0, 1, 3};
    for (unsigned inherited = 0; inherited < 2; ++inherited)
        for (size_t i = 0; i < 3; ++i) {
            string_value_t s = format_string(
                U"var result=false;const state={};state.saved=null;const proto={};"
                U"const a=%s;a.marker=81;proto[\"[]\"]=func(x){collect();"
                U"if(this!=a || this.marker!=81 || arguments.length!=%u)throw 91;"
                U"if(arguments.length>0 && (x!=11 || arguments[0]!=11))throw 92;"
                U"if(arguments.length>1 && (arguments[1]!=22 || arguments[2]!=33))throw 93;"
                U"state.saved=arguments;return a;};"
                U"proto[\"[]=\"]=func(x){collect();"
                U"if(this!=a || arguments.length!=%u)throw 94;"
                U"if(arguments.length>1 && arguments[0]!=11)throw 95;"
                U"if(arguments.length>2 && (arguments[1]!=22 || arguments[2]!=33))throw 96;"
                U"state.storedValue=arguments[arguments.length-1];state.saved=arguments;"
                U"x=null;collect();return 999;};"
                U"const read=a[%s];const value={};value.marker=82;"
                U"var assigned=null;assigned=a[%s]=value;collect();"
                U"result=read==a && assigned==value && state.storedValue==value "
                U"&& state.saved[%u]==value && state.saved.length==%u;",
                inherited ? U"proto -> {}" : U"proto",
                counts[i],
                counts[i] + 1,
                indices[i],
                indices[i],
                counts[i],
                counts[i] + 1);
            program(s.data, SETINDEX, 0);
            FREE_STRING(s);
        }
    program(U"var result=false;const a={};a.name=10;a[\"[]\"]=func(){return this;};"
            U"a[\"[]=\"]=func(){throw 99;};a.name=11;"
            U"result=a.name==11 && a[][1,2,3][4].name==11;",
            GETINDEX,
            0);
    program(U"var result=false;const a={};const f=func(){return 77;};"
            U"a[\"[]\"]=func(){return f;};result=a[]()==77;",
            GETINDEX,
            0);
}

static void order_case(void) {
    program(U"var result=false;var log=0;const old={};var a=old;"
            U"a[\"[]\"]=func(){throw 99;};"
            U"const receiver=func(){log=1;return a;};"
            U"const first=func(){log=log*10+2;a={};return 11;};"
            U"const last=func(){log=log*10+3;old[\"[]\"]=func(x,y){"
            U"log=log*10+4;return this==old && x==11 && y==22;};return 22;};"
            U"const value=receiver()[first(),last()];result=value && log==1234;",
            GETINDEX,
            0);
    program(U"var result=false;var log=0;const old={};var a=old;"
            U"const receiver=func(){log=1;return a;};"
            U"const first=func(){log=log*10+2;a={};return 11;};"
            U"const second=func(){log=log*10+3;return 22;};"
            U"const rhs=func(){log=log*10+4;old[\"[]=\"]=func(x,y,v){"
            U"log=log*10+5;if(this!=old || x!=11 || y!=22 || v!=77)throw 99;"
            U"return 999;};return 77;};"
            U"const value=receiver()[first(),second()]=rhs();result=value==77 && log==12345;",
            SETINDEX,
            0);
    program(U"var result=false;var log=0;const a={};const rhs=func(){log=1;"
            U"a[\"[]=\"]=func(value){log=2;};return 77;};"
            U"const value=a[]=rhs();result=value==77 && log==2;",
            SETINDEX,
            0);
    program(U"var result=false;var log=0;const a={};const index=func(){log=1;"
            U"a[\"[]=\"]=func(k,v){log=3;};return 11;};"
            U"const rhs=func(){log=2;return 77;};const value=a[index()]=rhs();"
            U"result=value==77 && log==3;",
            SETINDEX,
            0);
    program(U"var result=false;var log=0;const a={};a[\"[]\"]=func(){throw 99;};"
            U"const first=func(){log=1;throw 73;};const later=func(){log=9;};"
            U"try {a[first(),later()]=later();}catch(e){result=e==73 && log==1;}",
            SETINDEX,
            0);
    program(U"var result=false;var log=0;const first=func(){throw 73;};"
            U"const later=func(){log=9;};try {first()[later()];}"
            U"catch(e){result=e==73 && log==0;}",
            GETINDEX,
            0);
    program(U"var result=false;var x=0;const a={};a[\"[]\"]=func(){x=7;return 8;};"
            U"const b=a[];result=x==7 && b==8;",
            GETINDEX,
            0);
}

static void analysis_case(void) {
    arena_t *arena = create_arena(8);
    const char *json =
        "{\"externals\":{\"flag\":{\"mutable\":false,\"type\":\"boolean\",\"value\":false}}}";
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    assert(doc);
    module_interface_t interface = {.metadata = yyjson_doc_get_root(doc)};
    const lattice_element_t *module = make_module_scope_element(arena, &interface);
    variable_declaration_pair_t scope =
        create_synthetic_variable_declaration_node(arena, (string_view_t){U"scope", 5});
    variable_declaration_pair_t captured =
        create_synthetic_variable_declaration_node(arena, (string_view_t){U"captured", 8});
    expression_t *receiver = create_variable_node(arena, scope.declarator->name);
    ((variable_t *)receiver)->declarator = scope.declarator;
    node_t *index = create_index_access_node(arena, receiver);
    expression_t *key_node = (expression_t *)create_static_string_node(arena, U"flag", 4);
    set_index_arguments(index, arena, &key_node, 1);
    expression_t *member = create_member_access_node(arena, receiver, (string_view_t){U"flag", 4});
    for (unsigned reachability = 0; reachability < 2; ++reachability)
        for (unsigned indexed = 0; indexed < 2; ++indexed) {
            abstract_state_t *state = create_abstract_state(arena);
            set_in_abstract_state(state, scope.declarator, module);
            set_in_abstract_state(state, captured.declarator, make_true_element());
            node_t *node = indexed ? index : (node_t *)member;
            const lattice_element_t *result =
                reachability ? visit_reachable_node(node, &state, NULL)
                             : calculate_expression((expression_t *)node, state, arena);
            assert(result->type == (indexed ? LATTICE_TOP : LATTICE_FALSE));
            assert(get_from_abstract_state(state, captured.declarator)->type
                   == (indexed ? LATTICE_TOP : LATTICE_TRUE));
            destroy_abstract_state(state);
        }
    yyjson_doc_free(doc);
    destroy_arena(arena);
}

static void fallback_case(void) {
    program(U"var result=false;const a={};a[\"name\"]=17;const b=[10,20,30];"
            U"b[1]=22;result=a[\"name\"]==17 && a[\"absent\"]==null && b[1]==22;",
            GETINDEX,
            0);
    const gchar_t *values[] = {U"{}", U"[1,2,3]"};
    const gchar_t *indices[] = {U"", U"0,1,2"};
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < 2; ++j)
            for (unsigned store = 0; store < 2; ++store) {
                string_value_t s =
                    format_string(U"var result=false;const a=%s;try {a[%s]%s;}"
                                  U"catch(e){result=e==Exceptions.INVALID_ARGUMENT;}",
                                  values[i],
                                  indices[j],
                                  store ? U"=77" : U"");
                program(s.data, store ? SETINDEX : GETINDEX, 0);
                FREE_STRING(s);
            }
    program(U"var result=false;var effects=0;const a=[1];"
            U"const rhs=func(){effects+=1;return 77;};try {a[]=rhs();}"
            U"catch(e){result=e==Exceptions.INVALID_ARGUMENT && effects==0;}",
            SETINDEX,
            0);
}

static void lookup_case(void) {
    const gchar_t *bad[] = {U"null", U"false", U"3", U"\"x\"", U"[]", U"callable"};
    for (unsigned store = 0; store < 2; ++store)
        for (unsigned inherited = 0; inherited < 2; ++inherited)
            for (size_t j = 0; j < sizeof(bad) / sizeof(*bad); ++j) {
                string_value_t s = format_string(
                    U"var result=false;var effects=0;const proto={};const a=%s;"
                    U"const callable={};callable[\"()\"]=func(){throw 99;};"
                    U"proto[\"%s\"]=%s;const index=func(){effects+=1;return 11;};"
                    U"const rhs=func(){effects+=10;return 77;};"
                    U"try {a[index()]%s;}catch(e){result=e==Exceptions.INVALID_OPERATION && "
                    U"effects==%u;}",
                    inherited ? U"proto -> {}" : U"proto",
                    store ? U"[]=" : U"[]",
                    bad[j],
                    store ? U"=rhs()" : U"",
                    store ? 11 : 1);
                program(s.data, store ? SETINDEX : GETINDEX, 0);
                FREE_STRING(s);
            }
    for (unsigned store = 0; store < 2; ++store)
        for (unsigned inherited = 0; inherited < 2; ++inherited) {
            string_value_t s =
                format_string(U"var result=false;var effects=0;const a=%s;const "
                              U"index=func(){effects+=1;return 11;};"
                              U"const rhs=func(){effects+=10;return 77;};try {a[index()]%s;}"
                              U"catch(e){result=e==Exceptions.ACCESS_DENIED && effects==%u;}",
                              inherited ? U"restricted -> {}" : U"restricted",
                              store ? U"=rhs()" : U"",
                              store ? 11 : 1);
            program(s.data, store ? SETINDEX : GETINDEX, 0);
            FREE_STRING(s);
        }
}

static void mutation_case(void) {
    for (unsigned during_index = 0; during_index < 2; ++during_index) {
        string_value_t s =
            format_string(U"var result=false;var log=0;const a={};a[\"[]=\"]=func(){throw 99;};"
                          U"const newSetter=func(k,v){if(this!=a || k!=11 || v!=77)throw "
                          U"98;log+=100;return 999;};"
                          U"const index=func(){log+=1;%s return 11;};"
                          U"const rhs=func(){log+=10;%s return 77;};"
                          U"const value=a[index()]=rhs();result=value==77 && log==111;",
                          during_index ? U"replaceSetter(a,newSetter);" : U"",
                          during_index ? U"" : U"replaceSetter(a,newSetter);");
        program(s.data, SETINDEX, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;var effects=0;const a={};const rhs=func(){effects=1;return 77;};"
            U"try {a[]=rhs();}catch(e){result=e==Exceptions.INVALID_ARGUMENT && effects==1;}",
            SETINDEX,
            0);
    program(U"var result=false;const a={var secret=81;public getter=func(){return this.secret;};"
            U"public setter=func(value){this.secret=value;};};a[\"[]\"]=a.getter;"
            U"a[\"[]=\"]=a.setter;const value=a[]=82;result=value==82 && a[]==82;",
            SETINDEX,
            0);
}

static void continuation_case(void) {
    program(U"var result=false;var log=0;const a={};const b={};"
            U"b[\"[]\"]=func(x){collect();return x+1;};"
            U"a[\"[]\"]=func(x){collect();return b[x]+1;};"
            U"a[\"[]=\"]=func(x,v){try {log=a[v];return 999;}finally {log+=100;collect();}};"
            U"const outer=func(x,y,z){return x==97 && y==7 && z==98;};"
            U"result=outer(97,a[0]=7,98) && log==109;",
            SETINDEX,
            0);
    for (unsigned store = 0; store < 2; ++store) {
        string_value_t s = format_string(
            U"var result=false;var log=0;const a={};const payload={};"
            U"a[\"%s\"]=func(){try {log=1;throw payload;}finally {log+=10;collect();}};"
            U"var assigned=99;try {assigned=a[]%s;}"
            U"catch(e){result=e==payload && log==11 && assigned==99;}collect();",
            store ? U"[]=" : U"[]",
            store ? U"=77" : U"");
        program(s.data, store ? SETINDEX : GETINDEX, 0);
        FREE_STRING(s);
    }
    program(U"var result=false;const a={};a[\"[]=\"]=func(){try {return 99;}finally {return 88;}};"
            U"const value=a[]=77;result=value==77;",
            SETINDEX,
            0);
    program(U"const a={};a[\"[]=\"]=func(){const nested=func(){throw 73;};nested();};a[]=77;",
            SETINDEX,
            1);
    program(U"var result=false;var count=0;const a={};a[\"[]\"]=func(x){"
            U"count+=1;collect();if(x==0)return 0;return this[x-1]+1;};"
            U"result=a[40]==40 && count==41;",
            GETINDEX,
            0);
}

static void lifetime_case(void) {
    program(U"var result=false;const make=func(){const a={};a.marker=81;"
            U"a[\"[]=\"]=func(value){value=null;collect();return {};};return a;};"
            U"const rhs=func(){const b={};b.marker=82;return b;};"
            U"const value=make()[]=rhs();collect();result=value.marker==82;",
            SETINDEX,
            0);
    program(U"var result=false;const make=func(){const a={};a.marker=81;"
            U"a[\"[]\"]=probe;return a;};"
            U"const arg=func(n){const b={};b.marker=n;return b;};"
            U"result=make()[arg(82),arg(83),arg(84)];",
            GETINDEX,
            0);
    program(U"var result=false;const make=func(){const a={};a.marker=81;"
            U"a[\"[]=\"]=probe;return a;};"
            U"const arg=func(n){const b={};b.marker=n;return b;};"
            U"const value=make()[arg(82),arg(83),arg(84)]=arg(85);"
            U"collect();result=value.marker==85;",
            SETINDEX,
            0);
    program(U"var result=false;const a={};a[\"[]\"]=nonbooleanBuiltin;"
            U"a[\"[]=\"]=booleanBuiltin;const value=a[]=77;result=a[]==null && value==77;",
            SETINDEX,
            0);
    program(U"var result=false;const a={};const payload={};a[\"[]=\"]=failBuiltin;"
            U"try {a[]=payload;}catch(e){result=e==payload;}",
            SETINDEX,
            0);
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
 * index selection must preserve this error after evaluating indices and the RHS. */
static void failed_file_case(void) {
    static const struct {
        const gchar_t *sign, *opposite, *source;
    } operations[] = {{U"[]", NULL, U"target[rhs()];"}, {U"[]=", NULL, U"target[rhs()] = rhs();"}};

    static builtin_function_object_t handler_storage, rhs_storage;
    static const builtin_function_t rhs_descriptor = {.name = U"rhs", .execute = failed_rhs};
    static const builtin_function_t handler_descriptor = {.name = U"failedScopeHandler",
                                                          .execute = failed_scope_handler};
    object_t *handler = get_builtin_function_object(&handler_storage, &handler_descriptor);
    char source[] = "external ready = true;";
    module_source_t file = {.path = "index_failure.goat",
                            .name = "index_failure.goat",
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
        void *image = malloc(f.code->buffer_size);
        assert(image);
        memcpy(image, f.code->buffer, f.code->buffer_size);
        bytecode_t *loaded = decode_bytecode_image(image, f.code->buffer_size);
        assert(loaded);
        for (unsigned decoded = 0; decoded < 2; ++decoded)
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
                    int status = run(p, decoded ? loaded : f.code);
                    assert(
                        !memcmp(image, (decoded ? loaded : f.code)->buffer, f.code->buffer_size));
                    if (variant == 5) {
                        assert(failed_rhs_calls == op + 1);
                        /* A successfully selected own handler never consults the ancestor. */
                        assert(status == 0 && !t->exception.value
                               && failed_scope_handler_calls == 1);
                    } else {
                        assert(status != 0 && failed_scope_handler_calls == 0
                               && failed_rhs_calls == op + 1);
                        if (variant == 6 || variant == 7) {
                            /* Neither an invalid nor inaccessible own handler falls through. */
                            assert(t->exception.value
                                   == (variant == 6 ? get_exception_invalid_operation()
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
    }
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
}

static void module_case(void) {
    char main_source[] =
        "const target=include \"index.goat\";"
        "const value=target[\"flag\"];"
        "if(!value)throw 91;if(target.captured!=7)throw 93;if(target.flag!=false)throw 94;"
        "const assigned=target[\"flag\"]=82;"
        "if(assigned!=82 || target.captured!=82 || target.flag!=false)throw 92;";
    char helper_source[] = "external flag=false; external var captured=0;"
                           "external getter=func(key){captured=7;return true;};"
                           "external setter=func(key,value){captured=value;return 999;};";
    module_source_t source_files[] = {{.path = "main.goat",
                                       .name = "main.goat",
                                       .source = main_source,
                                       .length = sizeof(main_source) - 1},
                                      {.path = "index.goat",
                                       .name = "index.goat",
                                       .source = helper_source,
                                       .length = sizeof(helper_source) - 1}};
    source_include_error_t include_error = {0};
    assert(parse_source_includes("main.goat",
                                 main_source,
                                 sizeof(main_source) - 1,
                                 &source_files[0].includes,
                                 &include_error));
    assert(source_files[0].includes.count == 1);
    size_t target_file = 1;
    source_files[0].targets = &target_file;
    module_sources_t sources = {.files = source_files, .count = 2};
    for (unsigned optimized = 0; optimized < 2; ++optimized) {
        module_compilation_t *compilation = NULL;
        module_compilation_error_t error = {0};
        options_t *options = create_options();
        options->optimization_level = optimized ? OPTIMIZATION_ALL : OPTIMIZATION_NONE;
        bool compiled = compile_module_sources_configured(&sources,
                                                          NULL,
                                                          NULL,
                                                          options,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          &compilation,
                                                          &error);
        if (!compiled)
            fprintf(stderr, "module compile: %s\n", error.message ? error.message : "diagnostic");
        assert(compiled);
        destroy_options(options);
        bytecode_t *code = compilation->code;
        void *image = malloc(code->buffer_size);
        assert(image);
        memcpy(image, code->buffer, code->buffer_size);
        bytecode_t *loaded = decode_bytecode_image(image, code->buffer_size);
        assert(loaded);
        for (unsigned decoded = 0; decoded < 2; ++decoded) {
            process_t *p = create_process();
            module_files_t *files = NULL;
            assert(create_module_files_from_snapshot(p,
                                                     compilation->metadata,
                                                     decoded ? loaded : code,
                                                     &files));
            /* File scopes cannot be extended from source. The host supplies the
             * operator properties after initializing the real captured functions. */
            module_file_t *helper = module_files_get(files, 1);
            assert(run_module_file(p, helper) == 0);
            object_t *scope = module_file_scope(helper);
            object_t *getter = key(p, U"getter"), *setter = key(p, U"setter");
            field(p, scope, U"[]", get_object_property(scope, getter), ACCESS_EXTERNAL);
            field(p, scope, U"[]=", get_object_property(scope, setter), ACCESS_EXTERNAL);
            DECREF(getter);
            DECREF(setter);
            int status = run_module_file(p, module_files_main(files));
            if (status) {
                string_value_t e = convert_object_to_string(p->main_thread->exception.value);
                char *text = encode_utf8(e.data);
                fprintf(stderr,
                        "module run optimized=%u decoded=%u: %s\n",
                        optimized,
                        decoded,
                        text);
                FREE(text);
                FREE_STRING(e);
            }
            assert(status == 0 && !p->main_thread->exception.value);
            assert(p->main_thread->data_stack->size == 0);
            assert(!memcmp(image, (decoded ? loaded : code)->buffer, code->buffer_size));
            destroy_process(p);
        }
        free(image);
        free_bytecode(loaded);
        free_module_compilation(compilation);
        clear_module_compilation_error(&error);
    }
    clear_source_includes(&source_files[0].includes);
}

static unsigned arity_calls;

static operation_result_t arity_handler(object_t **args, uint16_t count, thread_t *t) {
    (void)t;
    assert(count == UINT16_MAX);
    for (size_t i = 0; i < count; ++i)
        assert(args[i] == get_null_object());
    ++arity_calls;
    return operation_success(get_null_object());
}

/* The image carries uint32 counts; the call ABI carries uint16 actual arity.
 * Test the largest valid count and first unrepresentable one without narrowing. */
static void wire_case(void) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"arity", .execute = arity_handler};
    for (unsigned store = 0; store < 2; ++store)
        for (unsigned overflow = 0; overflow < 2; ++overflow) {
            unsigned count = UINT16_MAX - store + overflow;
            code_builder_t *b = create_code_builder();
            data_builder_t *d = create_data_builder();
            uint32_t name = add_string_to_data_segment(d, U"target");
            if (store)
                add_instruction(b, (instruction_t){.opcode = NIL});
            add_instruction(b, (instruction_t){.opcode = VLOAD, .arg1 = name});
            for (unsigned i = 0; i < count; ++i)
                add_instruction(b, (instruction_t){.opcode = NIL});
            add_instruction(b,
                            (instruction_t){.opcode = store ? SETINDEX : GETINDEX, .arg1 = count});
            add_instruction(b, (instruction_t){.opcode = POP});
            add_instruction(b, (instruction_t){.opcode = END});
            bytecode_t *code = link_code_and_data(b, d);
            assert(code && validate_bytecode_image(code));
            void *image = malloc(code->buffer_size);
            assert(image);
            memcpy(image, code->buffer, code->buffer_size);
            bytecode_t *loaded = decode_bytecode_image(image, code->buffer_size);
            assert(loaded);
            for (unsigned decoded = 0; decoded < 2; ++decoded) {
                process_t *p = create_process();
                thread_t *t = p->main_thread;
                object_t *target = object(p, get_root_object());
                field(p,
                      target,
                      store ? U"[]=" : U"[]",
                      get_builtin_function_object(&storage, &descriptor),
                      ACCESS_EXTERNAL);
                field(p, t->context->data, U"target", target, ACCESS_EXTERNAL);
                DECREF(target);
                arity_calls = 0;
                int status = run(p, decoded ? loaded : code);
                assert((status != 0) == (overflow != 0));
                assert(arity_calls == !overflow);
                if (overflow)
                    assert(t->exception.value == get_exception_invalid_argument());
                assert(t->data_stack->size == 0);
                assert(!memcmp(image, (decoded ? loaded : code)->buffer, code->buffer_size));
                destroy_process(p);
            }
            free(image);
            free_bytecode(loaded);
            free_bytecode(code);
            destroy_code_builder(b);
            destroy_data_builder(d);
        }
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
    else if (!strcmp(group, "analysis"))
        analysis_case();
    else if (!strcmp(group, "module"))
        module_case();
    else if (!strcmp(group, "fallback"))
        fallback_case();
    else if (!strcmp(group, "lookup"))
        lookup_case();
    else if (!strcmp(group, "continuation"))
        continuation_case();
    else if (!strcmp(group, "lifetime"))
        lifetime_case();
    else if (!strcmp(group, "failed-file"))
        failed_file_case();
    else if (!strcmp(group, "wire"))
        wire_case();
    else
        assert(!"unknown index test case");
    printf("%s: %zu source programs, four executions each\n", group, program_number);
    return 0;
}
