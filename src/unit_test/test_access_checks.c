#include "lib/string_ext.h"
/** @file test_access_checks.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises two artificial modules through real bytecode and shared model operations.
 */
#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "model/context.h"
#include "model/thread.h"
#include "test_access_checks.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <stdio.h>

static object_t *key(process_t *proc, const goat_char_t *name) {
    return create_string_object(proc, (string_value_t){name, goat_strlen(name), false});
}

static field_cell_t *field(process_t *proc,
                           object_t *obj,
                           const goat_char_t *name,
                           object_t *value,
                           access_modifier_t modifier,
                           bool constant) {
    object_t *k = key(proc, name);
    field_cell_t *cell = create_object_field(obj,
                                             k,
                                             value,
                                             constant,
                                             modifier,
                                             get_object_module(obj),
                                             get_object_access_domain(obj),
                                             true);
    DECREF(k);
    return cell;
}

bool test_access_policy(void) {
    process_t *proc = create_process();
    module_t *foreign = create_module_identity();
    object_t *root = get_root_object();
    object_t *a = create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, foreign);
    object_t *b = create_user_defined_object(proc, (object_array_t){&a, 1});
    object_t *unrelated = create_user_defined_object(proc, (object_array_t){&root, 1});
    function_access_t owner, child;
    init_function_access(&owner, foreign);
    init_function_access(&child, proc->module);
    add_function_access_domain(&owner, get_object_access_domain(a));
    add_function_access_domain(&child, get_object_access_domain(b));
    add_protected_object_access(&child, b);
    const function_access_t *outside = &proc->main_thread->context->access;
    object_t *one = get_static_integer_object(1), *two = get_static_integer_object(2);
    field_cell_t *secret = field(proc, a, U"secret", one, ACCESS_PRIVATE, false);
    field_cell_t *prot = field(proc, a, U"prot", one, ACCESS_PROTECTED, false);
    field_cell_t *internal = field(proc, a, U"internal", one, ACCESS_MODULE, false);
    field_cell_t *pub = field(proc, a, U"pub", one, ACCESS_PUBLIC, false);
    field_cell_t *fixed = field(proc, a, U"fixed", one, ACCESS_PUBLIC, true);
    ASSERT(can_access_field(&owner, secret) && !can_access_field(&child, secret));
    ASSERT(can_access_field(&child, prot) && !can_access_field(outside, prot));
    ASSERT(can_access_field(&owner, internal) && !can_access_field(&child, internal));
    ASSERT(can_access_field(outside, pub));
    ASSERT(write_accessible_property(outside, a, pub->key, two) == MSTAT_OK);
    ASSERT(write_accessible_property(outside, a, fixed->key, two) == MSTAT_PROPERTY_IS_CONSTANT);
    ASSERT(write_accessible_property(outside, a, secret->key, two) == MSTAT_ACCESS_DENIED);
    object_t *new_key = key(proc, U"new");
    ASSERT(lookup_accessible_property(outside, a, new_key, true).status
           == MSTAT_PROPERTY_NOT_FOUND);
    ASSERT(write_accessible_property(outside, a, new_key, one) == MSTAT_FOREIGN_OBJECT);
    ASSERT(create_accessible_property(outside, a, new_key, one, false) == MSTAT_FOREIGN_OBJECT);
    ASSERT(create_accessible_property(&owner, a, new_key, one, false) == MSTAT_OK);
    field_cell_t *other = field(proc, unrelated, U"secret", two, ACCESS_PRIVATE, false);
    ASSERT(!can_access_field(&owner, other));
    /* An inaccessible winner must not fall back to a public prototype field. */
    field_cell_t *blocked = field(proc, b, U"pub", one, ACCESS_PRIVATE, false);
    ASSERT(lookup_accessible_property(outside, b, blocked->key, true).status
           == MSTAT_ACCESS_DENIED);
    ASSERT(write_accessible_property(outside, b, blocked->key, two) == MSTAT_ACCESS_DENIED);
    /* A method's own private slot is distinct from the descendant's ordinary winner. */
    field(proc, b, U"secret", two, ACCESS_PUBLIC, false);
    ASSERT(lookup_accessible_property(&owner, b, secret->key, true).cell == secret);
    ASSERT(lookup_accessible_property(&owner, b, secret->key, false).value == two);
    ASSERT(write_accessible_property(&owner, b, secret->key, two) == MSTAT_OK);
    /* Public constness never masks an access denial. */
    fixed->modifier = ACCESS_PRIVATE; /* Test setup only; no language mutation of metadata. */
    ASSERT(write_accessible_property(outside, a, fixed->key, two) == MSTAT_ACCESS_DENIED);
    /* Arbitrary keys use the same policy. */
    field_cell_t *arbitrary = create_object_field(a,
                                                  unrelated,
                                                  one,
                                                  false,
                                                  ACCESS_PRIVATE,
                                                  foreign,
                                                  get_object_access_domain(a),
                                                  true);
    ASSERT(arbitrary
           && lookup_accessible_property(outside, a, unrelated, true).status
                  == MSTAT_ACCESS_DENIED);
    /* Ownership of a copy changes without disclosing module-local fields or old private rights. */
    object_t *copy = clone_object_with_access(proc, a, outside);
    ASSERT(get_object_module(copy) == proc->module);
    ASSERT(lookup_accessible_property(outside, copy, internal->key, true).status
           == MSTAT_ACCESS_DENIED);
    ASSERT(lookup_accessible_property(&owner, copy, secret->key, true).status
           == MSTAT_ACCESS_DENIED);
    ASSERT(write_accessible_property(outside, copy, get_boolean_object(true), two) == MSTAT_OK);
    DECREF(copy);
    DECREF(new_key);
    clear_function_access(&owner, true);
    clear_function_access(&child, true);
    DECREF(b);
    DECREF(a);
    DECREF(unrelated);
    release_module_identity(foreign);
    destroy_process(proc);
    return true;
}

/** @brief Compiles a real method body, then installs it in module A before running module B.
 * This bypasses only declaration syntax, which belongs to #164, not any access operation.
 */
static void emit_functions(node_t *node, code_builder_t *builder, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, builder, data);
    for (size_t i = 0; i < get_node_child_count(node); i++)
        emit_functions(get_node_child(node, i), builder, data);
}

static bool run_case(const goat_char_t *body, const goat_char_t *main) {
    goat_char_t source[8192];
    goat_snprintf(source,
                  sizeof(source) / sizeof(*source),
                  U"const unused = func() { %s }; var result = false; %s",
                  body,
                  main);
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node =
        parse_analysis_test_program(&memory, (string_value_t){source, goat_strlen(source), false});
    ASSERT(node);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, builder, data);
    emit_functions(node, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    instr_index_t entry = BAD_INSTR_INDEX;
    for (size_t i = 1; i < code->instructions_count; i++) {
        if (code->instructions[i].opcode == FUNC && code->instructions[i - 1].opcode == ARG) {
            entry = code->instructions[i - 1].arg1;
            break;
        }
    }
    ASSERT(entry != BAD_INSTR_INDEX);
    process_t *proc = create_process();
    thread_t *thread = proc->main_thread;
    module_t *module = create_module_identity();
    object_t *root = get_root_context()->data;
    object_t *a = create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, module);
    object_t *one = get_static_integer_object(1), *ten = get_static_integer_object(10);
    field(proc, a, U"secret", ten, ACCESS_PRIVATE, false);
    field(proc, a, U"prot", ten, ACCESS_PROTECTED, false);
    field(proc, a, U"internal", ten, ACCESS_MODULE, false);
    field(proc, a, U"writable", one, ACCESS_PUBLIC, false);
    field(proc, a, U"fixed", one, ACCESS_PUBLIC, true);
    field(proc, a, U"callback", get_null_object(), ACCESS_PUBLIC, false);
    field(proc, a, U"other", get_null_object(), ACCESS_PUBLIC, false);
    field(proc, a, U"self", a, ACCESS_PUBLIC, true);
    object_t *array = create_array_object(proc, (object_array_t){&one, 1});
    field(proc, a, U"items", array, ACCESS_PRIVATE, false);
    DECREF(array);
    object_t *method = create_function_object(proc, NULL, 0, entry, a, NULL, true);
    field(proc, a, U"read", method, ACCESS_PUBLIC, true);
    field(proc, a, U"hidden", method, ACCESS_PRIVATE, true);
    DECREF(method);
    field(proc, thread->context->data, U"A", a, ACCESS_MODULE, true);
    DECREF(a);
    release_module_identity(module);
    int status = run(proc, code);
    object_t *result_key = key(proc, U"result");
    object_t *result = get_object_property(thread->context->data, result_key);
    bool ok = status == 0 && result && get_object_boolean_value(result)
              && thread->context->module == proc->module;
    if (!ok)
        fprintf(stderr, "Access VM case failed (%d)\n", status);
    DECREF(result_key);
    destroy_process(proc);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    return ok;
}

bool test_access_vm(void) {
    const struct {
        const goat_char_t *body;
        const goat_char_t *main;
    } cases[] = {
        {U"return secret;", U"result = A.read() == 10;"},
        {U"return self[\"secret\"];", U"const detached = A.read; result = detached() == 10;"},
        {U"return secret;", U"try A.secret; catch(e) result = e == Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var k=\"internal\"; try A[k]; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;", U"try A.secret=2; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;", U"try A[\"secret\"]+=2; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return ++secret;", U"result=A.read()==11;"},
        {U"return secret;", U"try A.extra=2; catch(e) result=e==Exceptions.FOREIGN_OBJECT;"},
        {U"return secret;", U"try A[true]=2; catch(e) result=e==Exceptions.FOREIGN_OBJECT;"},
        {U"return secret;", U"try A.fixed=2; catch(e) result=e==Exceptions.PROPERTY_IS_CONSTANT;"},
        {U"return secret;", U"A.writable+=2; result=A.writable==3;"},
        {U"return secret;",
         U"var calls=0; const obj=func(){calls+=1;return A;}; const key=func(){calls+=1;return "
         U"\"writable\";}; obj()[key()]+=4; result=calls==2 && A.writable==5;"},
        {U"return secret;",
         U"var calls=0; const obj=func(){calls+=1;return A;}; const key=func(){calls+=1;return "
         U"\"secret\";}; try obj()[key()]+=1; catch(e) result=calls==2 && "
         U"e==Exceptions.ACCESS_DENIED;"},
        {U"return func(){return secret;};", U"const escaped=A.read(); result=escaped()==10;"},
        {U"return {const get=func(){return secret;};};",
         U"var escaped=A.read(); try escaped.get; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return callback();",
         U"A.callback=func(){return A.secret;}; try A.read(); catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED;"},
        {U"throw secret;",
         U"try A.read(); catch(e) {if(e==10) {try A.secret; catch(f) "
         U"result=f==Exceptions.ACCESS_DENIED;}}"},
        {U"return secret;",
         U"A.read(); try A.secret; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return items;",
         U"var arr=A.read(); arr[0]=4; arr.push(5); result=A.read()[0]==4 && A.read()[1]==5;"},
        {U"return {};",
         U"var made=A.read(); try made.newField=1; catch(e) result=e==Exceptions.FOREIGN_OBJECT;"},
        {U"return secret;",
         U"var copy=A.clone(); copy.extra=3; try copy.internal; catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED && copy.extra==3;"},
        {U"return other.secret;",
         U"A.other=A.clone(); try A.read(); catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var child=A -> {const get=func(){return A.prot;};}; result=child.get()==10;"},
        {U"return secret;",
         U"var child=A -> {const get=func(){return A.secret;};}; try child.get(); catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var child=A -> {var secret=99;}; result=child.read()==10 && child.secret==99;"},
        {U"return secret;",
         U"var child=A -> {}; try child.internal; catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var child=A -> {var own=1;}; child.extra=2; result=child.own+child.extra==3;"},
        {U"return secret;",
         U"var child=A -> {}; child.get=func(){return A.prot;}; try child.get(); catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;", U"var copy={var read=A.read;}; result=copy.read()==10;"},
        {U"return internal;", U"result=A.read()==10;"},
        {U"return internal;",
         U"var copy=A.flat(); copy.extra=3; try copy.internal; catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED && copy.extra==3 && copy.read()==10;"},
        {U"return secret;",
         U"var child=A -> {const get=func(){return ++secret;};}; try child.get(); catch(e) "
         U"result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;", U"var child=A -> {var fixed=9;}; result=child.fixed==9;"},
        {U"return secret;",
         U"var child=A -> {const get=func(){return {const nested=func(){return A.prot;};};};}; "
         U"result=child.get().nested()==10;"},
        {U"return secret;",
         U"var child=A -> {const get=func(){return func(){return A.prot;};};}; const "
         U"escaped=child.get(); result=escaped()==10;"},
        {U"return callback();",
         U"A.callback=func(){throw 2;}; try A.read(); catch(e) {try A.internal; catch(f) "
         U"result=f==Exceptions.ACCESS_DENIED;} finally {try A.secret; catch(e) result=result && "
         U"e==Exceptions.ACCESS_DENIED;}"},
        {U"secret+=1; return secret;",
         U"var child=A -> {var secret=99;}; result=child.read()==11 && child.secret==99;"},
        {U"return secret;",
         U"var child=A -> {}; child.get=func(){try return A.prot; catch(e) return e;}; "
         U"result=child.get()==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var child=A -> {}; child.get=func(){return func(){return A.prot;};}; const "
         U"escaped=child.get(); try escaped(); catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var child=A -> {}; child.get=func(){return {const get=func(){return A.prot;};};}; const "
         U"escaped=child.get(); try escaped.get(); catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"var nested={const get=func(){return secret;};}; return nested.get;",
         U"const escaped=A.read(); result=escaped()==10;"},
        {U"return secret;", U"try A.hidden(); catch(e) result=e==Exceptions.ACCESS_DENIED;"},
        {U"return secret;",
         U"var k=\"hidden\"; try A[k](); catch(e) result=e==Exceptions.ACCESS_DENIED;"}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        if (!run_case(cases[i].body, cases[i].main)) {
            fprintf(stderr, "Access case %zu\n", i);
            return false;
        }
    }
    return true;
}
