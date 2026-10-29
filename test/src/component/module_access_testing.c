/** @file module_access_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief File/module visibility through model operations, real VM calls and wire images.
 */
#include "codegen/linker.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/vm.h"

#ifdef NDEBUG
#    undef NDEBUG /* Contract checks and fixture calls stay active in Release. */
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static object_t *key(process_t *p, const gchar_t *name) {
    return create_string_object(p, (string_value_t){name, STRLEN(name), false});
}

static field_cell_t *field(process_t *p,
                           object_t *obj,
                           const gchar_t *name,
                           object_t *value,
                           access_modifier_t modifier,
                           bool constant) {
    object_t *k = key(p, name);
    field_cell_t *cell = create_object_field_in_file(obj,
                                                     k,
                                                     value,
                                                     constant,
                                                     modifier,
                                                     get_object_module(obj),
                                                     get_object_file_id(obj),
                                                     get_object_access_domain(obj),
                                                     true);
    DECREF(k);
    assert(cell);
    return cell;
}

static object_t *object(process_t *p, module_t *module, uint32_t file) {
    object_t *root = get_root_object();
    return create_user_defined_object_in_file(p, (object_array_t){&root, 1}, module, file);
}

static void model_cases(void) {
    process_t *p = create_process();
    module_t *a = create_module_identity(), *b = create_module_identity();
    function_access_t rights[3];
    init_function_access_in_file(&rights[0], a, 7);
    init_function_access_in_file(&rights[1], a, 9);
    init_function_access_in_file(&rights[2], b, 7);
    object_t *obj = object(p, a, 7);
    object_t *one = get_static_integer_object(1), *two = get_static_integer_object(2);
    const gchar_t *names[] = {U"file", U"public", U"external", U"protected", U"private"};
    access_modifier_t modifiers[] = {ACCESS_FILE,
                                     ACCESS_PUBLIC,
                                     ACCESS_EXTERNAL,
                                     ACCESS_PROTECTED,
                                     ACCESS_PRIVATE};
    field_cell_t *cells[5];
    for (size_t i = 0; i < 5; ++i)
        cells[i] = field(p, obj, names[i], one, modifiers[i], false);
    const bool allowed[3][5] = {{true, true, true, false, false},
                                {false, true, true, false, false},
                                {false, false, true, false, false}};
    for (size_t who = 0; who < 3; ++who) {
        for (size_t i = 0; i < 5; ++i) {
            model_status_t expected = allowed[who][i] ? MSTAT_OK : MSTAT_ACCESS_DENIED;
            assert(can_access_field(&rights[who], cells[i]) == allowed[who][i]);
            assert(lookup_accessible_property(&rights[who], obj, cells[i]->key, false).status
                   == expected);
            field_cell_t before = *cells[i];
            assert(write_accessible_property(&rights[who], obj, cells[i]->key, two) == expected);
            /* Rules 9–10: writers replace values, never declaration identity or rights. */
            assert(get_own_field_cell(obj, before.key) == cells[i]);
            assert(cells[i]->key == before.key && cells[i]->module == before.module);
            assert(cells[i]->file_id == before.file_id && cells[i]->domain == before.domain);
            assert(cells[i]->modifier == before.modifier);
            assert(cells[i]->is_constant == before.is_constant);
            assert(cells[i]->binding_unit == before.binding_unit);
            assert(cells[i]->declaration_id == before.declaration_id);
            assert(cells[i]->pending_initialization == before.pending_initialization);
            assert(cells[i]->object == (allowed[who][i] ? two : before.object));
        }
        object_t *keys = snapshot_object_keys_with_access(p, obj, &rights[who]);
        assert(get_array_elements(keys).size == 3 - who);
        DECREF(keys);
    }
    object_t *nested = create_array_object(p, (object_array_t){&obj, 1});
    const gchar_t *formatted[] = {U"[{\"external\":2,\"file\":2,\"public\":2}]",
                                  U"[{\"external\":2,\"public\":2}]",
                                  U"[{\"external\":2}]"};
    for (size_t i = 0; i < 3; ++i) {
        string_value_t text = object_to_string_with_access(nested, &rights[i]);
        assert(!STRCMP(text.data, formatted[i]));
        FREE_STRING(text);
    }
    DECREF(nested);
    field_cell_t *fixed = field(p, obj, U"fixed", one, ACCESS_PUBLIC, true);
    assert(write_accessible_property(&rights[1], obj, fixed->key, two)
           == MSTAT_PROPERTY_IS_CONSTANT);
    assert(write_accessible_property(&rights[2], obj, fixed->key, two) == MSTAT_ACCESS_DENIED);
    object_t *fresh = key(p, U"fresh");
    assert(write_accessible_property(&rights[1], obj, fresh, one) == MSTAT_OK);
    field_cell_t *added = get_own_field_cell(obj, fresh);
    assert(added->module == a && added->file_id == 9 && added->modifier == ACCESS_FILE);
    assert(!can_access_field(&rights[0], added) && can_access_field(&rights[1], added));
    assert(write_accessible_property(&rights[0], obj, fresh, two) == MSTAT_ACCESS_DENIED);
    object_t *absent = key(p, U"absent");
    assert(create_accessible_property(&rights[2], obj, absent, one, false) == MSTAT_FOREIGN_OBJECT);
    assert(!get_own_field_cell(obj, absent));
    DECREF(absent);
    DECREF(fresh);
    object_t *derived = create_user_defined_object_in_file(p, (object_array_t){&obj, 1}, a, 9);
    field(p, derived, U"external", one, ACCESS_FILE, false);
    assert(lookup_accessible_property(&rights[2], derived, cells[2]->key, false).status
           == MSTAT_ACCESS_DENIED);
    assert(write_accessible_property(&rights[2], derived, cells[2]->key, two)
           == MSTAT_ACCESS_DENIED);
    add_function_access_domain(&rights[1], get_object_access_domain(obj));
    assert(can_access_field(&rights[1], cells[3]) && can_access_field(&rights[1], cells[4]));
    object_t *method = create_function_object(p, NULL, 0, 0, obj, NULL, true);
    assert(get_function_access(method)->file_id == 7);
    field(p, obj, U"method", method, ACCESS_EXTERNAL, true);
    for (size_t flat = 0; flat < 2; ++flat) {
        object_t *copy = flat ? flat_object_with_access(p, obj, &rights[2])
                              : clone_object_with_access(p, obj, &rights[2]);
        assert(get_object_module(copy) == b && get_object_file_id(copy) == 7);
        for (size_t i = 0; i < 3; ++i) {
            field_cell_t *c = get_own_field_cell(copy, cells[i]->key);
            assert(c->module == a && c->file_id == 7 && c->modifier == modifiers[i]);
            assert(can_access_field(&rights[2], c) == (i == 2));
        }
        object_t *mk = key(p, U"method");
        object_t *adapted = get_own_field_cell(copy, mk)->object;
        function_access_t *access = get_function_access(adapted);
        assert(access->module == a && access->file_id == 7);
        assert(can_access_field(access, get_own_field_cell(copy, cells[0]->key)));
        DECREF(mk);
        add_gc_root(p, copy);
        add_gc_root(p, obj);
        add_gc_root(p, derived);
        collect_garbage(p);
        assert(get_function_access(adapted)->file_id == 7);
        remove_gc_root(p, copy);
        remove_gc_root(p, obj);
        remove_gc_root(p, derived);
        DECREF(copy);
    }
    DECREF(method);
    DECREF(derived);
    DECREF(obj);
    for (size_t i = 0; i < 3; ++i)
        clear_function_access(&rights[i], true);
    release_module_identity(a);
    release_module_identity(b);
    destroy_process(p);
    assert(!get_allocated_memory_size());
}

static void emit_functions(node_t *node, code_builder_t *builder, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, builder, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        emit_functions(get_node_child(node, i), builder, data);
}

static void vm_case(const gchar_t *body, const gchar_t *main, bool foreign) {
    gchar_t source[8192];
    SNPRINTF(source,
             sizeof(source) / sizeof(*source),
             U"const unused=func(){%s}; var result=false; %s",
             body,
             main);
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node =
        parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    if (!node)
        FPRINTF(stderr, "Could not parse: %S\n", source);
    assert(node);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, builder, data);
    emit_functions(node, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    instr_index_t entry = BAD_INSTR_INDEX;
    for (size_t i = 1; i < code->instructions_count; ++i)
        if (code->instructions[i].opcode == FUNC && code->instructions[i - 1].opcode == ARG) {
            entry = code->instructions[i - 1].arg1;
            break;
        }
    assert(entry != BAD_INSTR_INDEX);
    process_t *p = create_process();
    context_t *caller = p->main_thread->context;
    module_t *module = foreign ? create_module_identity() : retain_module_identity(p->module);
    object_t *root = get_root_context()->data;
    object_t *a = create_user_defined_object_in_file(p, (object_array_t){&root, 1}, module, 7);
    field(p, a, U"local", get_static_integer_object(10), ACCESS_FILE, false);
    field(p, a, U"pub", get_static_integer_object(20), ACCESS_PUBLIC, false);
    field(p, a, U"ext", get_static_integer_object(30), ACCESS_EXTERNAL, false);
    field(p, a, U"self", a, ACCESS_EXTERNAL, true);
    field(p, a, U"callback", get_null_object(), ACCESS_EXTERNAL, false);
    object_t *method = create_function_object(p, NULL, 0, entry, a, NULL, true);
    field(p, a, U"read", method, ACCESS_EXTERNAL, true);
    field(p, caller->data, U"A", a, ACCESS_FILE, true);
    DECREF(method);
    DECREF(a);
    release_module_identity(module);
    int status = run(p, code);
    object_t *k = key(p, U"result");
    object_t *result = get_object_property(caller->data, k);
    if (status || !result || !get_object_boolean_value(result))
        FPRINTF(stderr, "Module access VM failed: %S / %S\n", body, main);
    assert(!status && result && get_object_boolean_value(result));
    assert(p->main_thread->context == caller && caller->access.file_id == 0);
    DECREF(k);
    destroy_process(p);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    assert(!get_allocated_memory_size());
}

static void vm_cases(void) {
    for (size_t foreign = 0; foreign < 2; ++foreign) {
        vm_case(U"return local;", U"result=A.read()==10;", foreign);
        vm_case(U"return local;", U"const f=A.read; result=f()==10;", foreign);
        vm_case(U"return func(){return local;};", U"const f=A.read(); result=f()==10;", foreign);
        vm_case(U"return self.callback();",
                U"A.callback=func(){return A.local;}; try A.read(); catch(e) "
                U"result=e==Exceptions.ACCESS_DENIED;",
                foreign);
        vm_case(U"return local;",
                U"try A.local; catch(e) result=e==Exceptions.ACCESS_DENIED;",
                foreign);
        vm_case(U"return local;",
                U"var k=\"local\"; try A[k]=99; catch(e) result=e==Exceptions.ACCESS_DENIED;",
                foreign);
        vm_case(U"return local;",
                U"result=A.ext==30; A.ext=31; result=result && A.ext==31;",
                foreign);
        vm_case(U"return local;",
                U"const b=A.clone(); const good=b.read()==10; try b.local; catch(e) result=good && "
                U"e==Exceptions.ACCESS_DENIED;",
                foreign);
        vm_case(U"return local;",
                U"const b=A.flat(); const good=b.read()==10; try b.local; catch(e) result=good && "
                U"e==Exceptions.ACCESS_DENIED;",
                foreign);
    }
    vm_case(U"return self.added;",
            U"A.added=55; try A.read(); catch(e) result=e==Exceptions.ACCESS_DENIED;",
            false);
    vm_case(U"self.added=55; return self.added;",
            U"const good=A.read()==55; try A.added; catch(e) result=good && "
            U"e==Exceptions.ACCESS_DENIED;",
            false);
    vm_case(U"return pub;", U"result=A.pub==20; A.pub=21; result=result && A.read()==21;", false);
    vm_case(U"return pub;", U"try A.pub; catch(e) result=e==Exceptions.ACCESS_DENIED;", true);
    vm_case(
        U"return local;",
        U"var n=0; for(const k in A) {if(k==\"local\") n+=100; if(k==\"ext\") n+=1;} result=n==1;",
        true);
}

static void wire_cases(void) {
    unsigned char *input = malloc(1024 * 1024);
    assert(input);
    size_t size = FREAD(input, 1, 1024 * 1024, stdin);
    assert(size > 4 && size < 1024 * 1024);
    size_t json = read_le32(input);
    assert(json < size - 4);
    process_t *p = create_process();
    module_files_t *sets[2];
    for (size_t s = 0; s < 2; ++s)
        assert(!create_module_files(p,
                                    (char *)input + 4,
                                    json,
                                    input + 4 + json,
                                    size - 4 - json,
                                    &sets[s],
                                    NULL));
    size_t image_size = size - 4 - json;
    unsigned char *copy = malloc(image_size);
    assert(copy);
    memcpy(copy, input + 4 + json, image_size);
    free(input);
    object_t *x = key(p, U"x");
    add_gc_root(p, x);
    for (size_t s = 0; s < 2; ++s) {
        for (size_t i = 0; i < module_files_count(sets[s]); ++i) {
            module_file_t *file = module_files_get(sets[s], (uint32_t)i);
            context_t *caller = p->main_thread->context;
            context_t *ctx = create_module_file_context(file, caller);
            assert(ctx->access.file_id == i && get_object_file_id(ctx->data) == i);
            p->main_thread->context = ctx;
            p->main_thread->instr_id = module_file_entry(file);
            assert(!run(p, module_files_bytecode(sets[s])));
            field_cell_t *cell = get_own_field_cell(ctx->data, x);
            assert(cell && cell->file_id == i && cell->module == module_files_identity(sets[s]));
            instruction_t declaration =
                module_files_bytecode(sets[s])->instructions[module_file_entry(file) + 1];
            access_modifier_t modifier = ACCESS_FILE;
            bool constant = declaration.opcode == DCONST;
            if (declaration.opcode == DVAR || constant) {
                declaration_descriptor_t descriptor;
                assert(get_bytecode_declaration(module_files_bytecode(sets[s]),
                                                declaration.arg1,
                                                &descriptor));
                assert(descriptor.file == i);
                modifier = (access_modifier_t)descriptor.modifier;
                assert(cell->declaration_id == declaration.arg1 + 1 && cell->binding_unit);
            }
            assert(cell->modifier == modifier && cell->is_constant == constant);
            for (size_t t = 0; t < 2; ++t) {
                for (size_t j = 0; j < module_files_count(sets[t]); ++j) {
                    function_access_t rights;
                    init_function_access_in_file(&rights,
                                                 module_files_identity(sets[t]),
                                                 (uint32_t)j);
                    bool allowed = modifier == ACCESS_EXTERNAL
                                   || (s == t && (modifier == ACCESS_PUBLIC || i == j));
                    assert(can_access_field(&rights, cell) == allowed);
                    assert(lookup_accessible_property(&rights, ctx->data, x, false).status
                           == (allowed ? MSTAT_OK : MSTAT_ACCESS_DENIED));
                    model_status_t write_status = !allowed   ? MSTAT_ACCESS_DENIED
                                                  : constant ? MSTAT_PROPERTY_IS_CONSTANT
                                                             : MSTAT_OK;
                    assert(write_accessible_property(&rights,
                                                     ctx->data,
                                                     x,
                                                     get_static_integer_object(42))
                           == write_status);
                    clear_function_access(&rights, true);
                }
            }
            assert(destroy_context(ctx) == caller);
            p->main_thread->context = caller;
        }
    }
    collect_garbage(p);
    for (size_t s = 0; s < 2; ++s) {
        assert(!memcmp(copy, module_files_bytecode(sets[s])->buffer, image_size));
        for (size_t i = 0; i < module_files_count(sets[s]); ++i)
            assert(get_own_field_cell(module_file_scope(module_files_get(sets[s], (uint32_t)i)), x)
                       ->file_id
                   == i);
    }
    remove_gc_root(p, x);
    DECREF(x);
    free(copy);
    destroy_process(p);
    assert(!get_allocated_memory_size());
}

int main(int argc, char **argv) {
    (void)argv;
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (argc > 1)
        wire_cases();
    else {
        model_cases();
        vm_cases();
    }
    return 0;
}
