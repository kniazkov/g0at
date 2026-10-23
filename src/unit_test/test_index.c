/** @file test_index.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Indexed access from parser through virtual dispatch and object lifetime.
 */
#include "test_index.h"

#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "graph/index_access.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "model/common_methods.h"
#include "model/context.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/binary.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <stdio.h>

bool test_index_parser(void) {
    const gchar_t *valid[] = {U"a[1]",
                              U"a[]",
                              U"a[1, 2, 3] = 4",
                              U"a.b[1].c[2]",
                              U"a[1][2].c",
                              U"a[b[c[d]]]",
                              U"f()[1]",
                              U"(a)[1]",
                              U"a[1](2)[3].f(4)[5]",
                              U"a[1 + 2, f(3), (4 * 5)]",
                              U"a[x = 1] = b[y = 2] = 3",
                              U"a[func() { return 1 }]",
                              U"a[{ var x = 1 }]",
                              U"a[1]()(2)",
                              U"var a = { this[true] = 1 }",
                              U"(a[1])[2]",
                              U"a[1] + b[2] * c[3]"};
    const gchar_t *invalid[] = {U"a[",
                                U"a[1)",
                                U"a[,1]",
                                U"a[1,]",
                                U"a[1,,2]",
                                U"a[1 2]",
                                U"a[;]",
                                U"a[1;2]",
                                U"a[+]",
                                U"a[1].",
                                U"a[1] =",
                                U"a[1](,2)",
                                U"a[1](2,)",
                                U"a[1 +]",
                                U"a[return 1]"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root =
            parse_analysis_test_program(&memory,
                                        (string_value_t){valid[i], STRLEN(valid[i]), false});
        ASSERT(root);
        string_value_t source = generate_goat_code_from_node(root);
        ASSERT(parse_analysis_test_program(&memory, source));
        FREE_STRING(source);
        destroy_arena(arena);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(
            !parse_analysis_test_program(&memory,
                                         (string_value_t){invalid[i], STRLEN(invalid[i]), false}));
        destroy_arena(arena);
    }
    return true;
}

/** @brief Test-only two-dimensional receiver; proves the VM imposes no dictionary arity. */
static operation_result_t read_matrix(process_t *process, object_t *obj, object_array_t indices) {
    if (indices.size != 2)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        create_integer_object(process,
                              get_object_integer_value(indices.items[0]).value * 10
                                  + get_object_integer_value(indices.items[1]).value));
}

/** @brief Returns a different value to verify owned result propagation from the setter. */
static operation_result_t
write_matrix(process_t *process, object_t *obj, object_array_t indices, object_t *value) {
    if (indices.size != 2)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        create_integer_object(process,
                              get_object_integer_value(value).value
                                  + get_object_integer_value(indices.items[0]).value * 10
                                  + get_object_integer_value(indices.items[1]).value));
}

bool test_index_dispatch(void) {
    object_vtbl_t vtbl = *get_root_object()->vtbl;
    vtbl.get_index = read_matrix;
    vtbl.set_index = write_matrix;
    object_t matrix = {.vtbl = &vtbl};
    for (int store = 0; store < 2; store++) {
        process_t *proc = create_process();
        object_stack_t *stack = proc->main_thread->data_stack;
        push_object_onto_stack(stack, get_static_integer_object(99));
        if (store)
            push_object_onto_stack(stack, get_static_integer_object(100));
        push_object_onto_stack(stack, &matrix);
        push_object_onto_stack(stack, get_static_integer_object(4));
        push_object_onto_stack(stack, get_static_integer_object(2));
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        add_instruction(builder, (instruction_t){.opcode = store ? SETINDEX : GETINDEX, .arg1 = 2});
        add_instruction(builder, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(builder, data);
        destroy_code_builder(builder);
        destroy_data_builder(data);
        const char *path = "goat-index-unit.gbin";
        ASSERT(save_binary_program(path, code, NULL));
        binary_program_t saved = load_binary_program(path);
        ASSERT(saved.code);
        ASSERT(saved.code->instructions[0].arg1 == 2);
        ASSERT(run(proc, saved.code) == 0);
        ASSERT(stack->size == 2);
        ASSERT(get_object_integer_value(peek_object_from_stack(stack, 0)).value
               == (store ? 142 : 42));
        ASSERT(get_object_integer_value(peek_object_from_stack(stack, 1)).value == 99);
        destroy_binary_program(&saved);
        free_bytecode(code);
        destroy_process(proc);
        remove(path);
    }
    return true;
}

/** @brief Creates a dictionary rooted at the supplied prototype. */
static object_t *dictionary(process_t *proc, object_t *proto) {
    return create_user_defined_object(proc, (object_array_t){&proto, 1});
}

bool test_index_model(void) {
    process_t *proc = create_process();
    object_t *obj = dictionary(proc, get_root_object());
    add_gc_root(proc, obj);
    object_t *key = dictionary(proc, get_root_object());
    object_t *value = dictionary(proc, get_root_object());
    operation_result_t result = obj->vtbl->set_index(proc, obj, (object_array_t){&key, 1}, value);
    ASSERT(!result.is_exception && result.value == value);
    DECREF(result.value);
    DECREF(key);
    DECREF(value);
    collect_garbage(proc);
    result = obj->vtbl->get_index(proc, obj, (object_array_t){&key, 1});
    ASSERT(!result.is_exception && result.value == value);
    DECREF(result.value);
    object_t *clone = clone_object(proc, obj);
    add_gc_root(proc, clone);
    result = clone->vtbl->get_index(proc, clone, (object_array_t){&key, 1});
    ASSERT(result.value == value);
    DECREF(result.value);
    ASSERT(get_object_property(obj, get_this_key()) == obj);
    ASSERT(get_object_property(clone, get_this_key()) == clone);
    ASSERT(get_object_keys(obj).size == 1);
    result = obj->vtbl->get_index(proc, obj, (object_array_t){NULL, 0});
    ASSERT(result.is_exception && result.value == get_exception_invalid_argument());
    DECREF(result.value);
    object_t *indices[] = {key, value};
    result = obj->vtbl->set_index(proc, obj, (object_array_t){indices, 2}, obj);
    ASSERT(result.is_exception && result.value == get_exception_invalid_argument());
    DECREF(result.value);
    object_t *self = dictionary(proc, get_root_object());
    add_gc_root(proc, self);
    result = self->vtbl->set_index(proc, self, (object_array_t){&self, 1}, self);
    ASSERT(result.value == self);
    DECREF(result.value);
    string_value_t text = convert_object_to_string_notation(self);
    ASSERT(!STRCMP(text.data, U"{{...}:{...}}"));
    FREE_STRING(text);
    collect_garbage(proc);
    remove_gc_root(proc, self);
    DECREF(self);
    collect_garbage(proc);
    remove_gc_root(proc, clone);
    DECREF(clone);
    collect_garbage(proc);
    result = obj->vtbl->get_index(proc, obj, (object_array_t){&key, 1});
    ASSERT(result.value == value);
    DECREF(result.value);
    remove_gc_root(proc, obj);
    DECREF(obj);
    destroy_process(proc);
    return true;
}
