/** @file operator_dispatch_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Operator foundation contracts, independent of arithmetic opcode dispatch.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "common/module_compilation.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/exception.h"
#include "model/module_files.h"
#include "model/object_stack.h"
#include "model/operator.h"
#include "model/process.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static object_t *key(process_t *p, const gchar_t *s) {
    return create_string_object(p, (string_value_t){s, STRLEN(s), false});
}

static field_cell_t *
field(process_t *p, object_t *o, const gchar_t *name, object_t *v, access_modifier_t mode) {
    object_t *k = key(p, name);
    field_cell_t *c = create_object_field(o,
                                          k,
                                          v,
                                          false,
                                          mode,
                                          get_object_module(o),
                                          get_object_access_domain(o),
                                          true);
    DECREF(k);
    assert(c);
    return c;
}

static object_t *object(process_t *p, object_t *proto) {
    return create_user_defined_object(p, (object_array_t){&proto, 1});
}

static void expect_lookup(const function_access_t *a,
                          object_t *o,
                          object_t *k,
                          operator_lookup_status_t status,
                          object_t *value) {
    operator_lookup_t found = lookup_operator_handler(a, o, k);
    assert(found.status == status);
    if (status == OPERATOR_FOUND)
        assert(found.function == value && !found.exception.value);
    if (status == OPERATOR_ERROR)
        assert(found.exception.value == value && !found.function);
    clear_operator_lookup(&found);
    clear_operator_lookup(&found);
}

static bool fake_call(object_t *o, uint16_t n, object_t *r, thread_t *t) {
    (void)o;
    (void)n;
    (void)r;
    (void)t;
    assert(!"lookup must not call arbitrary objects");
    return false;
}

static void lookup_case(void) {
    process_t *p = create_process();
    const function_access_t *a = &p->main_thread->context->access;
    object_t *k = key(p, U"+");
    object_t *proto = object(p, get_root_object());
    object_t *child = object(p, proto);
    object_t *fn = create_function_object(p, NULL, 0, 0, p->main_thread->context->data, NULL, true);
    assert(is_function_object(fn));
    assert(is_function_object(get_function_sign()));
    assert(!is_function_object(child));
    object_vtbl_t fake_vtbl = *get_null_object()->vtbl;
    fake_vtbl.type = TYPE_OTHER;
    fake_vtbl.call = fake_call;
    object_t fake = {.vtbl = &fake_vtbl};
    assert(!is_function_object(&fake));
    expect_lookup(a, child, k, OPERATOR_ABSENT, NULL);
    expect_lookup(a, get_static_integer_object(1), k, OPERATOR_NOT_APPLICABLE, NULL);
    field(p, proto, U"+", fn, ACCESS_EXTERNAL);
    expect_lookup(a, child, k, OPERATOR_FOUND, fn);
    field_cell_t *own = field(p, child, U"+", get_null_object(), ACCESS_EXTERNAL);
    expect_lookup(a, child, k, OPERATOR_ERROR, get_exception_invalid_operation());
    assert(set_field_cell_value(child, own, get_static_integer_object(3)) == MSTAT_OK);
    expect_lookup(a, child, k, OPERATOR_ERROR, get_exception_invalid_operation());
    object_t *callable = object(p, get_root_object());
    field(p, callable, U"()", fn, ACCESS_EXTERNAL);
    assert(set_field_cell_value(child, own, callable) == MSTAT_OK);
    expect_lookup(a, child, k, OPERATOR_ERROR, get_exception_invalid_operation());
    assert(set_field_cell_value(child, own, get_function_sign()) == MSTAT_OK);
    expect_lookup(a, child, k, OPERATOR_FOUND, get_function_sign());
    own->modifier = ACCESS_PRIVATE; /* Test fixture metadata, not language mutation. */
    expect_lookup(a, child, k, OPERATOR_ERROR, get_exception_access_denied());
    function_access_t owner;
    init_function_access(&owner, p->module);
    add_function_access_domain(&owner, get_object_access_domain(child));
    expect_lookup(&owner, child, k, OPERATOR_FOUND, get_function_sign());

    /* Selected handlers remain valid when the property changes before invocation. */
    own->modifier = ACCESS_EXTERNAL;
    assert(set_field_cell_value(child, own, fn) == MSTAT_OK);
    operator_lookup_t saved = lookup_operator_handler(a, child, k);
    assert(saved.status == OPERATOR_FOUND);
    assert(set_field_cell_value(child, own, get_null_object()) == MSTAT_OK);
    assert(saved.function == fn && is_function_object(saved.function));
    clear_operator_lookup(&saved);
    /* Ordinary prototype order, including competing direct ancestors. */
    object_t *second = object(p, get_root_object());
    field(p, second, U"+", get_function_sign(), ACCESS_EXTERNAL);
    object_t *ancestors[] = {proto, second};
    object_t *multiple = create_user_defined_object(p, (object_array_t){ancestors, 2});
    property_lookup_t ordinary = lookup_accessible_property(a, multiple, k, true);
    assert(ordinary.status == MSTAT_OK);
    expect_lookup(a, multiple, k, OPERATOR_FOUND, ordinary.value);
    DECREF(multiple);
    DECREF(second);
    module_t *foreign = create_module_identity();
    object_t *root = get_root_object();
    object_t *foreign_object =
        create_user_defined_object_in_file(p, (object_array_t){&root, 1}, foreign, 7);
    field_cell_t *restricted = field(p, foreign_object, U"+", fn, ACCESS_EXTERNAL);
    restricted->file_id = 7;
    function_access_t same_file, other_file, descendant;
    init_function_access_in_file(&same_file, foreign, 7);
    init_function_access_in_file(&other_file, foreign, 8);
    init_function_access(&descendant, p->module);
    add_protected_object_access(&descendant, foreign_object);
    restricted->modifier = ACCESS_FILE;
    expect_lookup(&same_file, foreign_object, k, OPERATOR_FOUND, fn);
    expect_lookup(&other_file, foreign_object, k, OPERATOR_ERROR, get_exception_access_denied());
    expect_lookup(a, foreign_object, k, OPERATOR_ERROR, get_exception_access_denied());
    restricted->pending_initialization = true;
    expect_lookup(&same_file,
                  foreign_object,
                  k,
                  OPERATOR_ERROR,
                  get_exception_uninitialized_declaration());
    restricted->pending_initialization = false;
    restricted->modifier = ACCESS_PUBLIC;
    expect_lookup(&other_file, foreign_object, k, OPERATOR_FOUND, fn);
    expect_lookup(a, foreign_object, k, OPERATOR_ERROR, get_exception_access_denied());
    restricted->modifier = ACCESS_PROTECTED;
    expect_lookup(&descendant, foreign_object, k, OPERATOR_FOUND, fn);
    expect_lookup(a, foreign_object, k, OPERATOR_ERROR, get_exception_access_denied());
    clear_function_access(&same_file, true);
    clear_function_access(&other_file, true);
    clear_function_access(&descendant, true);
    DECREF(foreign_object);
    release_module_identity(foreign);
    clear_function_access(&owner, true);
    DECREF(callable);
    DECREF(fn);
    DECREF(child);
    DECREF(proto);
    DECREF(k);
    destroy_process(p);
}

typedef struct {
    arena_t *arena;
    code_builder_t *builder;
    data_builder_t *data;
    bytecode_t *code;
    instr_index_t body, resume;
} fixture_t;

static void deferred(node_t *n, code_builder_t *b, data_builder_t *d) {
    if (n->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(n, b, d);
    for (size_t i = 0; i < get_node_child_count(n); ++i)
        deferred(get_node_child(n, i), b, d);
}

static fixture_t compile(const gchar_t *body) {
    fixture_t f = {0};
    gchar_t source[8192];
    SNPRINTF(source, sizeof(source) / sizeof(*source), U"const unused = func() { %s };", body);
    f.arena = create_arena(8);
    parser_memory_t m = {f.arena, f.arena, f.arena, f.arena};
    node_t *n = parse_analysis_test_program(&m, (string_value_t){source, STRLEN(source), false});
    assert(n);
    f.builder = create_code_builder();
    f.data = create_data_builder();
    generate_bytecode_from_node(n, f.builder, f.data);
    deferred(n, f.builder, f.data);
    f.code = link_code_and_data(f.builder, f.data);
    f.body = f.resume = BAD_INSTR_INDEX;
    for (size_t i = 0; i < f.code->instructions_count; i++) {
        if (f.code->instructions[i].opcode == END && f.resume == BAD_INSTR_INDEX)
            f.resume = i;
        if (i && f.code->instructions[i].opcode == FUNC && f.body == BAD_INSTR_INDEX)
            f.body = f.code->instructions[i - 1].arg1;
    }
    assert(f.body != BAD_INSTR_INDEX && f.resume != BAD_INSTR_INDEX);
    return f;
}

static void dispose(fixture_t *f) {
    free_bytecode(f->code);
    destroy_code_builder(f->builder);
    destroy_data_builder(f->data);
    destroy_arena(f->arena);
}

static void dynamic_case(const gchar_t *body,
                         operator_result_policy_t policy,
                         bool receiver_result,
                         int expected,
                         bool throws) {
    fixture_t f = compile(body);
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    context_t *caller = t->context;
    object_t *receiver = object(p, get_root_object());
    object_t *fn = create_function_object(p, NULL, 0, f.body, caller->data, NULL, true);
    field(p, caller->data, U"original", receiver, ACCESS_EXTERNAL);
    push_object_onto_stack(t->data_stack, get_static_integer_object(97));
    t->instr_id = 0;
    assert(call_operator_handler(fn, receiver, 0, policy, f.resume, t));
    assert(t->context != caller && t->instr_id == f.body);
    DECREF(fn); /* The deferred call must survive destruction of the function value. */
    collect_garbage(p);
    if (throws)
        DECREF(receiver); /* Uncaught unwind removes caller GC roots. */
    int status = run(p, f.code);
    if (throws) {
        assert(status != 0 && t->exception.value);
        assert(t->exception.value == get_static_integer_object(expected));
        assert(exception_trace_size(t->exception.trace) > 0);
    } else {
        assert(status == 0 && t->context == caller && t->instr_id == f.resume);
        size_t result_count = policy == OPERATOR_RESULT_DISCARD ? 0 : 1;
        assert(t->data_stack->size == 1 + result_count);
        assert(t->data_stack->objects[0] == get_static_integer_object(97));
        if (result_count) {
            object_t *v = peek_object_from_stack(t->data_stack, 0);
            if (receiver_result)
                assert(v == receiver);
            else if (policy == OPERATOR_RESULT_BOOL || policy == OPERATOR_RESULT_NEGATED_BOOL)
                assert(v == get_boolean_object(expected != 0));
            else
                assert(v == get_static_integer_object(expected));
        }
    }
    if (!throws)
        DECREF(receiver);
    destroy_process(p);
    dispose(&f);
}

static void receiver_case(void) {
    dynamic_case(U"return this;", OPERATOR_RESULT_VALUE, true, 0, false);
    dynamic_case(U"const self=this; const nested=func(){return self;}; return nested();",
                 OPERATOR_RESULT_VALUE,
                 true,
                 0,
                 false);
    dynamic_case(U"{ if(this == original) throw 901; } return 11;",
                 OPERATOR_RESULT_VALUE,
                 false,
                 11,
                 false);
    dynamic_case(
        U"const nested=func(){return this == original;}; if(nested()) throw 902; return 12;",
        OPERATOR_RESULT_VALUE,
        false,
        12,
        false);
}

static void continuation_case(void) {
    dynamic_case(U"return 7;", OPERATOR_RESULT_VALUE, false, 7, false);
    dynamic_case(U"const nested=func(){return 9;}; return nested();",
                 OPERATOR_RESULT_VALUE,
                 false,
                 9,
                 false);
    dynamic_case(U"try { return 4; } finally { const nested=func(){return 8;}; nested(); }",
                 OPERATOR_RESULT_VALUE,
                 false,
                 4,
                 false);
    dynamic_case(U"try { return 4; } finally { return 6; }",
                 OPERATOR_RESULT_VALUE,
                 false,
                 6,
                 false);
    dynamic_case(U"return true;", OPERATOR_RESULT_BOOL, false, 1, false);
    dynamic_case(U"return true;", OPERATOR_RESULT_NEGATED_BOOL, false, 0, false);
    dynamic_case(U"return 9;", OPERATOR_RESULT_DISCARD, false, 0, false);
}

static void unwind_case(void) {
    dynamic_case(U"throw 71;", OPERATOR_RESULT_VALUE, false, 71, true);
    dynamic_case(U"const nested=func(){throw 72;}; return nested();",
                 OPERATOR_RESULT_VALUE,
                 false,
                 72,
                 true);
    dynamic_case(U"try { throw 73; } finally { const f=func(){return 1;}; f(); }",
                 OPERATOR_RESULT_BOOL,
                 false,
                 73,
                 true);
    dynamic_case(U"try { throw 73; } finally { throw 74; }",
                 OPERATOR_RESULT_VALUE,
                 false,
                 74,
                 true);
}

/* An ordinary CALL-only bridge supplies the future opcode's entry edge. It is
 * deliberately not a function and must never be accepted as an operator handler. */
static object_t bridge;
static object_vtbl_t bridge_vtbl;
static operator_result_policy_t bridge_policy;
static object_t *bridge_receiver;

static bool bridge_call(object_t *o, uint16_t count, object_t *r, thread_t *t) {
    (void)o;
    (void)r;
    assert(count);
    object_t *handler = pop_object_from_stack(t->data_stack);
    bool result = call_operator_handler(handler,
                                        bridge_receiver,
                                        count - 1,
                                        bridge_policy,
                                        t->instr_id + 1,
                                        t);
    DECREF(handler);
    return result;
}

static void program_case(const gchar_t *source, operator_result_policy_t policy) {
    fixture_t f = {0};
    f.arena = create_arena(8);
    parser_memory_t m = {f.arena, f.arena, f.arena, f.arena};
    node_t *n = parse_analysis_test_program(&m, (string_value_t){source, STRLEN(source), false});
    assert(n);
    f.builder = create_code_builder();
    f.data = create_data_builder();
    generate_bytecode_from_node(n, f.builder, f.data);
    deferred(n, f.builder, f.data);
    f.code = link_code_and_data(f.builder, f.data);
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    bridge_vtbl = *get_null_object()->vtbl;
    bridge_vtbl.type = TYPE_OTHER;
    bridge_vtbl.call = bridge_call;
    bridge.vtbl = &bridge_vtbl;
    assert(!is_function_object(&bridge));
    bridge_policy = policy;
    bridge_receiver = object(p, get_root_object());
    field(p, bridge_receiver, U"secret", get_static_integer_object(42), ACCESS_PRIVATE);
    field(p, t->context->data, U"bridge", &bridge, ACCESS_EXTERNAL);
    field(p, t->context->data, U"original", bridge_receiver, ACCESS_EXTERNAL);
    context_t *caller = t->context;
    int status = run(p, f.code);
    if (status)
        fprintf(stderr, "Operator bridge program failed\n");
    assert(status == 0 && t->context == caller && !t->exception.value);
    object_t *k = key(p, U"result");
    assert(get_object_property(t->context->data, k) == get_boolean_object(true));
    DECREF(k);
    DECREF(bridge_receiver);
    bridge_receiver = NULL;
    destroy_process(p);
    dispose(&f);
}

static void nested_case(void) {
    program_case(U"var result=false; const inner=func(){return this;}; "
                 U"const outer=func(){const value=bridge(inner); return value==this;}; "
                 U"result=bridge(outer);",
                 OPERATOR_RESULT_VALUE);
    program_case(
        U"var result=false; var log=0; const inner=func(){log=log*10+2;return 8;}; "
        U"const outer=func(){try {log=1;return 7;} finally {bridge(inner); log=log*10+3;}}; "
        U"result=bridge(outer)==7 && log==123;",
        OPERATOR_RESULT_VALUE);
    program_case(
        U"var result=false; var log=0; const thrown={}; "
        U"const inner=func(){throw thrown;}; const outer=func(){try {return bridge(inner);} "
        U"finally {log=1;}}; try {bridge(outer);} catch(e) {result=e==thrown && log==1;} ",
        OPERATOR_RESULT_VALUE);
    program_case(U"var result=false; var log=0; const bad=func(){try {return 5;} "
                 U"catch(e) {log=99;return true;} finally {log=1;}}; "
                 U"try {bridge(bad);} catch(e) {result=e==Exceptions.INVALID_OPERATION && log==1;}",
                 OPERATOR_RESULT_BOOL);
    program_case(U"var result=false; const handler=func(a){a=99;return arguments;}; "
                 U"const values=bridge(3,4,null,handler); "
                 U"result=values[0]==3 && values[1]==4 && values[2]==null;",
                 OPERATOR_RESULT_VALUE);
    program_case(U"var result=false; const handler=func(){return arguments;}; "
                 U"const values=bridge(handler); result=values.length==0;",
                 OPERATOR_RESULT_VALUE);
}

static void lexical_case(void) {
    fixture_t f = compile(U"const self=this; const nested=func(){return secret;}; "
                          U"if(self != original) throw 99; return nested();");
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    object_t *closure = object(p, t->context->data);
    field(p, closure, U"secret", get_static_integer_object(42), ACCESS_PRIVATE);
    object_t *receiver = object(p, closure);
    field(p, receiver, U"secret", get_static_integer_object(99), ACCESS_EXTERNAL);
    field(p, t->context->data, U"original", receiver, ACCESS_EXTERNAL);
    object_t *fn = create_function_object(p, NULL, 0, f.body, closure, NULL, true);
    field(p, closure, U"+", fn, ACCESS_EXTERNAL);
    object_t *k = key(p, U"+");
    operator_lookup_t selected = lookup_operator_handler(&t->context->access, receiver, k);
    assert(selected.status == OPERATOR_FOUND && selected.function == fn);
    assert(
        call_operator_handler(selected.function, receiver, 0, OPERATOR_RESULT_VALUE, f.resume, t));
    clear_operator_lookup(&selected);
    DECREF(k);
    DECREF(fn);
    collect_garbage(p);
    assert(run(p, f.code) == 0);
    assert(peek_object_from_stack(t->data_stack, 0) == get_static_integer_object(42));
    k = key(p, U"secret");
    assert(lookup_accessible_property(&t->context->access, closure, k, true).status
           == MSTAT_ACCESS_DENIED);
    DECREF(k);
    DECREF(receiver);
    DECREF(closure);
    destroy_process(p);
    dispose(&f);
    /* Installing a foreign closure as a handler must not grant receiver-private access. */
    program_case(U"var result=false; const handler=func(){return this.secret;}; "
                 U"try {bridge(handler);} catch(e) {result=e==Exceptions.ACCESS_DENIED;}",
                 OPERATOR_RESULT_VALUE);
}

static operation_result_t builtin_plain(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    assert(!"operator receiver must reach builtin method executor");
    return operation_exception(get_exception_invalid_operation());
}

static operation_result_t
builtin_method(object_t *receiver, object_t **args, uint16_t count, thread_t *t) {
    (void)t;
    assert(receiver && count == 2);
    assert(args[0] == get_static_integer_object(3) && args[1] == get_static_integer_object(4));
    INCREF(receiver);
    return operation_success(receiver);
}

static void builtin_case(void) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"test_operator",
                                                  .min_args = 2,
                                                  .execute = builtin_plain,
                                                  .execute_method = builtin_method};
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    object_t *receiver = object(p, get_root_object());
    object_t *fn = get_builtin_function_object(&storage, &descriptor);
    context_t *caller = t->context;
    push_object_onto_stack(t->data_stack, get_static_integer_object(97));
    push_object_onto_stack(t->data_stack, get_static_integer_object(3));
    push_object_onto_stack(t->data_stack, get_static_integer_object(4));
    assert(call_operator_handler(fn, receiver, 2, OPERATOR_RESULT_VALUE, 123, t));
    assert(t->context == caller && t->instr_id == 123 && t->data_stack->size == 2);
    assert(peek_object_from_stack(t->data_stack, 0) == receiver);
    object_t *returned = pop_object_from_stack(t->data_stack);
    DECREF(returned);
    push_object_onto_stack(t->data_stack, get_static_integer_object(3));
    push_object_onto_stack(t->data_stack, get_static_integer_object(4));
    assert(!call_operator_handler(fn, receiver, 2, OPERATOR_RESULT_BOOL, 456, t));
    assert(t->exception.value == get_exception_invalid_operation());
    assert(t->context == caller && t->data_stack->size == 1);
    assert(exception_trace_frame(t->exception.trace, 0)->instruction == 123);
    assert(t->instr_id == 123);
    clear_exception(&t->exception);
    DECREF(receiver);
    destroy_process(p);
}

static operation_result_t force_gc(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    collect_garbage(t->process);
    return operation_success(get_null_object());
}

static void lifetime_case(void) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"collect", .execute = force_gc};
    fixture_t f = compile(U"const self=this; try { const nested=func(){collect();return 1;}; "
                          U"nested(); return self; } "
                          U"finally {collect();}");
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    field(p,
          t->context->data,
          U"collect",
          get_builtin_function_object(&storage, &descriptor),
          ACCESS_EXTERNAL);
    object_t *receiver = object(p, get_root_object());
    field(p, receiver, U"marker", get_static_integer_object(83), ACCESS_EXTERNAL);
    object_t *fn = create_function_object(p, NULL, 0, f.body, t->context->data, NULL, true);
    assert(call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_VALUE, f.resume, t));
    DECREF(fn);
    DECREF(receiver);
    /* This raw identity is not a model reference and cannot root the receiver. */
    collect_garbage(p);
    assert(run(p, f.code) == 0);
    object_t *returned = peek_object_from_stack(t->data_stack, 0);
    assert(returned == receiver);
    object_t *k = key(p, U"marker");
    assert(get_object_property(returned, k) == get_static_integer_object(83));
    DECREF(k);
    destroy_process(p);
    dispose(&f);
}

static operation_result_t
builtin_gc_method(object_t *receiver, object_t **args, uint16_t count, thread_t *t) {
    assert(count == 1);
    object_t *arg = args[0];
    collect_garbage(t->process);
    object_t *k = key(t->process, U"marker");
    assert(get_object_property(receiver, k) == get_static_integer_object(81));
    assert(get_object_property(arg, k) == get_static_integer_object(82));
    DECREF(k);
    INCREF(receiver);
    return operation_success(receiver);
}

static void builtin_gc_case(void) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"gc_operator",
                                                  .min_args = 1,
                                                  .execute = builtin_plain,
                                                  .execute_method = builtin_gc_method};
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    object_t *receiver = object(p, get_root_object());
    object_t *arg = object(p, get_root_object());
    field(p, receiver, U"marker", get_static_integer_object(81), ACCESS_EXTERNAL);
    field(p, arg, U"marker", get_static_integer_object(82), ACCESS_EXTERNAL);
    push_object_onto_stack(t->data_stack, arg); /* Transfer sole argument reference. */
    assert(call_operator_handler(get_builtin_function_object(&storage, &descriptor),
                                 receiver,
                                 1,
                                 OPERATOR_RESULT_VALUE,
                                 123,
                                 t));
    assert(peek_object_from_stack(t->data_stack, 0) == receiver);
    DECREF(receiver);
    destroy_process(p);
}

static void result_error_trace_case(void) {
    fixture_t f = compile(U"return 3;");
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    object_t *receiver = object(p, get_root_object());
    object_t *fn = create_function_object(p, NULL, 0, f.body, t->context->data, NULL, true);
    const instr_index_t origin = 1;
    t->instr_id = origin;
    assert(f.resume != origin);
    assert(call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_BOOL, f.resume, t));
    DECREF(receiver);
    DECREF(fn);
    assert(run(p, f.code) != 0);
    assert(t->exception.value == get_exception_invalid_operation());
    assert(exception_trace_size(t->exception.trace));
    assert(exception_trace_frame(t->exception.trace, 0)->instruction == origin);
    destroy_process(p);
    dispose(&f);
}

static const exception_trace_t *builtin_saved_trace;
static bool builtin_throws;

static operation_result_t builtin_policy(object_t **args, uint16_t count, thread_t *t) {
    (void)args;
    (void)count;
    (void)t;
    if (builtin_throws)
        return (operation_result_t){get_static_integer_object(67), true, builtin_saved_trace};
    return operation_success(get_boolean_object(true));
}

static void builtin_policy_case(void) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"policy_operator",
                                                  .execute = builtin_policy};
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    object_t *receiver = object(p, get_root_object());
    object_t *fn = get_builtin_function_object(&storage, &descriptor);
    builtin_throws = false;
    assert(call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_BOOL, 12, t));
    assert(pop_object_from_stack(t->data_stack) == get_boolean_object(true));
    assert(call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_NEGATED_BOOL, 13, t));
    assert(pop_object_from_stack(t->data_stack) == get_boolean_object(false));
    assert(call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_DISCARD, 14, t));
    assert(t->data_stack->size == 0 && t->instr_id == 14);
    builtin_saved_trace = capture_exception_trace(t->context, 9);
    builtin_throws = true;
    assert(!call_operator_handler(fn, receiver, 0, OPERATOR_RESULT_BOOL, 15, t));
    assert(t->exception.value == get_static_integer_object(67));
    assert(t->exception.trace == builtin_saved_trace);
    assert(t->data_stack->size == 0);
    clear_exception(&t->exception);
    release_exception_trace(builtin_saved_trace);
    builtin_saved_trace = NULL;
    DECREF(receiver);
    destroy_process(p);
}

static bool
fail_initializer(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    if (data)
        ++*(unsigned *)data;
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
 * comparing that object must preserve this error rather than silently use identity. */
static void failed_file_comparisons(void) {
    static const struct {
        const gchar_t *sign, *opposite, *source;
    } operations[] = {{U"==", U"!=", U"target==target;"},
                      {U"!=", U"==", U"target!=target;"},
                      {U"<", NULL, U"target<target;"},
                      {U"<=", NULL, U"target<=target;"},
                      {U">", NULL, U"target>target;"},
                      {U">=", NULL, U"target>=target;"},
                      {U"!", NULL, U"!target;"}};

    static builtin_function_object_t handler_storage;
    static const builtin_function_t handler_descriptor = {.name = U"failedScopeHandler",
                                                          .execute = failed_scope_handler};
    object_t *handler = get_builtin_function_object(&handler_storage, &handler_descriptor);
    char source[] = "external ready = true;";
    module_source_t file = {.path = "comparison_failure.goat",
                            .name = "comparison_failure.goat",
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
                int status = run(p, f.code);
                if (variant == 5) {
                    /* A successfully selected own handler never consults the ancestor. */
                    assert(status == 0 && !t->exception.value && failed_scope_handler_calls == 1);
                } else {
                    assert(status != 0 && failed_scope_handler_calls == 0);
                    if (variant == 6 || variant == 7) {
                        /* Neither an invalid nor inaccessible own handler falls through. */
                        assert(t->exception.value
                               == (variant == 6 ? get_exception_invalid_operation()
                                                : get_exception_access_denied()));
                        assert(t->exception.trace && t->exception.trace != original.trace);
                    } else {
                        /* Includes absent requested/opposite handlers and the absent ! case:
                         * failed lookup must not become identity, negation or opposite fallback. */
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

static void failed_file_case(void) {
    failed_file_comparisons();
    char source[] = "external ready = true;";
    module_source_t file = {.path = "operator_failure.goat",
                            .name = "operator_failure.goat",
                            .source = source,
                            .length = sizeof(source) - 1};
    module_sources_t sources = {.files = &file, .count = 1};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compilation, &error));
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compilation->metadata, compilation->code, &files));
    module_file_t *failed = module_files_main(files);
    object_t *scope = module_file_scope(failed);
    field(p, scope, U"+", get_function_sign(), ACCESS_EXTERNAL);
    object_t *derived = object(p, scope);
    object_t *result = NULL;
    exception_t original = {0};
    assert(initialize_module_file(failed, t->context, fail_initializer, NULL, &result, &original)
           == MODULE_INITIALIZE_FAILED);
    assert(!result && original.value && original.trace);
    object_t *k = key(p, U"+");
    object_t *targets[] = {scope, derived};
    for (size_t i = 0; i < 2; ++i) {
        operator_lookup_t found = lookup_operator_handler(&t->context->access, targets[i], k);
        assert(found.status == OPERATOR_ERROR && !found.function);
        assert(found.exception.value == original.value && found.exception.trace == original.trace);
        clear_operator_lookup(&found);
        assert(module_file_exception(failed).trace == original.trace);
    }
    DECREF(derived);
    INCREF(k);
    push_object_onto_stack(t->data_stack, k);
    collect_garbage(p);
    operator_lookup_t repeated = lookup_operator_handler(&t->context->access, scope, k);
    assert(repeated.exception.value == original.value
           && repeated.exception.trace == original.trace);
    clear_operator_lookup(&repeated);
    clear_exception(&original);
    object_t *held_key = pop_object_from_stack(t->data_stack);
    DECREF(held_key);
    DECREF(k);
    destroy_process(p);
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
}

/** Real module images keep declaration identity even when a handler is installed
 * on an object belonging to another module and file. */
static void module_provenance_case(void) {
    char empty[] = "external ready=true;";
    char body[] = "private secret=42; external handler=func(x){"
                  "const nested=func(){return secret;};"
                  "if(nested()!=42) throw 901; x=null; return arguments;};";
    module_source_t source[] = {
        {.path = "owner.goat", .name = "owner.goat", .source = empty, .length = sizeof(empty) - 1},
        {.path = "handler.goat",
         .name = "handler.goat",
         .source = body,
         .length = sizeof(body) - 1}};
    module_sources_t sources = {.files = source, .count = 2};
    module_compilation_t *compilation = NULL;
    module_compilation_error_t error = {0};
    assert(compile_module_sources(&sources, &compilation, &error));
    process_t *p = create_process();
    thread_t *t = p->main_thread;
    context_t *caller = t->context;
    module_files_t *owner = NULL, *other = NULL;
    assert(create_module_files_from_snapshot(p, compilation->metadata, compilation->code, &owner));
    assert(create_module_files_from_snapshot(p, compilation->metadata, compilation->code, &other));
    assert(module_files_identity(owner) != module_files_identity(other));
    module_file_t *declaring = module_files_get(owner, 1);
    assert(run_module_file(p, declaring) == 0);
    object_t *handler_key = key(p, U"handler");
    object_t *handler = get_object_property(module_file_scope(declaring), handler_key);
    assert(handler && get_function_access(handler)->module == module_files_identity(owner));
    assert(get_function_access(handler)->file_id == 1);
    object_t *receiver = module_file_scope(module_files_get(other, 0));
    field(p, receiver, U"+", handler, ACCESS_EXTERNAL);
    object_t *operator_key = key(p, U"+");
    operator_lookup_t selected = lookup_operator_handler(&caller->access, receiver, operator_key);
    assert(selected.status == OPERATOR_FOUND && selected.function == handler);
    object_t *argument = object(p, get_root_object());
    field(p, argument, U"marker", get_static_integer_object(83), ACCESS_EXTERNAL);
    push_object_onto_stack(t->data_stack, argument); /* Sole live value reference. */
    fixture_t resume = compile(U"return null;");
    assert(call_operator_handler(selected.function,
                                 receiver,
                                 1,
                                 OPERATOR_RESULT_VALUE,
                                 resume.resume,
                                 t));
    assert(t->context->module == module_files_identity(owner));
    assert(t->context->access.file_id == 1 && t->context->call_receiver == receiver);
    clear_operator_lookup(&selected);
    DECREF(operator_key);
    DECREF(handler_key);
    collect_garbage(p);
    assert(run(p, resume.code) == 0 && t->context == caller);
    collect_garbage(p); /* Escaped array alone now roots the original object. */
    object_t *array = peek_object_from_stack(t->data_stack, 0);
    object_t *index = get_static_integer_object(0);
    operation_result_t element = array->vtbl->get_index(p, array, (object_array_t){&index, 1});
    assert(!element.is_exception && element.value == argument);
    object_t *marker = key(p, U"marker");
    assert(get_object_property(element.value, marker) == get_static_integer_object(83));
    DECREF(marker);
    DECREF(element.value);
    array = pop_object_from_stack(t->data_stack);
    DECREF(array);

    /* The receiver's file fails independently. This must not poison an already
       selected function whose declaring file is healthy. */
    object_t *scope = NULL;
    exception_t original = {0};
    module_file_t *failed = module_files_get(other, 0);
    unsigned initializer_calls = 0;
    assert(initialize_module_file(failed,
                                  caller,
                                  fail_initializer,
                                  &initializer_calls,
                                  &scope,
                                  &original)
           == MODULE_INITIALIZE_FAILED);
    assert(call_operator_handler(handler, receiver, 0, OPERATOR_RESULT_VALUE, resume.resume, t));
    assert(t->context->module == module_files_identity(owner));
    assert(run(p, resume.code) == 0 && !t->exception.value);
    array = pop_object_from_stack(t->data_stack);
    DECREF(array);

    /* A failed dynamic caller is equally irrelevant to a healthy declaration.
       Resume at the caller image's END, not an address from the owner's image. */
    context_t *failed_caller = create_module_file_context(failed, caller);
    t->context = failed_caller;
    instr_index_t end = module_file_entry(failed);
    while (module_file_bytecode(failed)->instructions[end].opcode != END) {
        ++end;
        assert(end < module_file_bytecode(failed)->instructions_count);
    }
    assert(call_operator_handler(handler, receiver, 0, OPERATOR_RESULT_VALUE, end, t));
    assert(t->context->module == module_files_identity(owner));
    assert(run(p, resume.code) == 0 && t->context == failed_caller && !t->exception.value);
    array = pop_object_from_stack(t->data_stack);
    DECREF(array);
    t->context = destroy_context(failed_caller);
    assert(t->context == caller);

    collect_garbage(p);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        exception_t repeated = {0};
        assert(initialize_module_file(failed,
                                      caller,
                                      fail_initializer,
                                      &initializer_calls,
                                      &scope,
                                      &repeated)
               == MODULE_INITIALIZE_FAILED);
        assert(!scope && initializer_calls == 1);
        assert(repeated.value == original.value && repeated.trace == original.trace);
        clear_exception(&repeated);
    }

    /* Conversely, installing a function from that failed file onto a healthy
       receiver must preserve the cached value AND trace, without entering it. */
    object_t *blocked =
        create_function_object(p, NULL, 0, 0, module_file_scope(failed), NULL, true);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        assert(!call_operator_handler(blocked,
                                      module_file_scope(declaring),
                                      0,
                                      OPERATOR_RESULT_VALUE,
                                      resume.resume,
                                      t));
        assert(t->context == caller && t->exception.value == original.value);
        assert(t->exception.trace == original.trace);
        const exception_trace_frame_t *origin = exception_trace_frame(t->exception.trace, 0);
        assert(origin->module == module_files_identity(other) && origin->file_id == 0);
        clear_exception(&t->exception);
    }
    DECREF(blocked);
    clear_exception(&original);
    destroy_process(p);
    dispose(&resume);
    free_module_compilation(compilation);
    clear_module_compilation_error(&error);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "lookup"))
        lookup_case();
    else if (!strcmp(argv[1], "receiver"))
        receiver_case();
    else if (!strcmp(argv[1], "continuation"))
        continuation_case();
    else if (!strcmp(argv[1], "unwind"))
        unwind_case();
    else if (!strcmp(argv[1], "nested"))
        nested_case();
    else if (!strcmp(argv[1], "builtin"))
        builtin_case();
    else if (!strcmp(argv[1], "lexical"))
        lexical_case();
    else if (!strcmp(argv[1], "lifetime"))
        lifetime_case();
    else if (!strcmp(argv[1], "builtin-gc"))
        builtin_gc_case();
    else if (!strcmp(argv[1], "result-trace"))
        result_error_trace_case();
    else if (!strcmp(argv[1], "builtin-policy"))
        builtin_policy_case();
    else if (!strcmp(argv[1], "failed-file")) {
        failed_file_case();
        module_provenance_case();
    } else
        assert(!"unknown operator foundation case");
    assert(!get_allocated_memory_size());
    puts("Operator foundation checks passed");
    return 0;
}
