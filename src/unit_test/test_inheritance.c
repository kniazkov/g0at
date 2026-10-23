/** @file test_inheritance.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Syntax, allocation-free prototype lists, and prototype ownership.
 */
#include "test_inheritance.h"

#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "model/process.h"
#include "test_macro.h"
#include "vm/binary.h"
#include "vm/gc.h"

#include <stdio.h>

bool test_inheritance_parser(void) {
    const goat_char_t *valid[] = {U"A -> {}",
                                  U"[A,B,] -> {}",
                                  U"[] -> {}",
                                  U"(A) -> { x=1; }",
                                  U"f(1) -> {}",
                                  U"a.b[1] -> {}",
                                  U"(A -> {}).x",
                                  U"(A -> {})[1]",
                                  U"A -> {} -> {}",
                                  U"A -> { var v = B -> {}; }",
                                  U"[A -> {},B] -> {}",
                                  U"f(A -> {})",
                                  U"[A -> {},B -> {}]",
                                  U"[A /*comment*/, B,] -> {}",
                                  U"var v = A -> { this[true]=1; }",
                                  U"A -> { const f=func(){return x;} }",
                                  U"a = b = A -> {}",
                                  U"([A,B]) -> {}",
                                  U"[f(1),f(2)] -> {}"};
    const goat_char_t *invalid[] = {U"-> {}",
                                    U"A ->",
                                    U"A -> 1",
                                    U"A -> f()",
                                    U"A -> []",
                                    U"A -> ({})",
                                    U"A -> func() {}",
                                    U"[A,,B] -> {}",
                                    U"[A B] -> {}",
                                    U"[,A] -> {}",
                                    U"[A;B] -> {}",
                                    U"A -> -> {}",
                                    U"A -> {",
                                    U"A -> {} = 1",
                                    U"[var a=1] -> {}",
                                    U"[return A] -> {}"};
    for (size_t pass = 0; pass < 2; pass++) {
        const goat_char_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root = parse_analysis_test_program(
                &memory,
                (string_value_t){texts[i], goat_strlen(texts[i]), false});
            ASSERT(pass ? root == NULL : root != NULL);
            if (root) {
                string_value_t source = generate_goat_code_from_node(root);
                ASSERT(parse_analysis_test_program(&memory, source));
                FREE_STRING(source);
            }
            destroy_arena(arena);
        }
    }
    return true;
}

bool test_inheritance_bytecode(void) {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(U"[A,B] -> {}"));
    ASSERT(root);
    node_t *expr = get_node_child(get_node_child(root, 0), 0);
    ASSERT(expr->vtbl->type == NODE_INHERITANCE);
    ASSERT(get_node_child(expr, 0)->vtbl->type == NODE_PROTOTYPE_LIST);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(root, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    size_t inherited = 0;
    for (size_t i = 0; i < code->instructions_count; i++) {
        instruction_t in = code->instructions[i];
        ASSERT(in.opcode != ARRAY);
        if (in.opcode == INHERIT) {
            ASSERT(in.arg0 == 1 && in.arg1 == 2);
            inherited++;
        }
    }
    ASSERT(inherited == 1);
    const char *path = "goat-inheritance-unit.gbin";
    ASSERT(save_binary_program(path, code, NULL));
    binary_program_t saved = load_binary_program(path);
    ASSERT(saved.code);
    destroy_binary_program(&saved);
    for (size_t i = 0; i < code->instructions_count; i++) {
        if (code->instructions[i].opcode != INHERIT)
            continue;
        code->instructions[i].arg0 = 2;
        ASSERT(save_binary_program(path, code, NULL));
        saved = load_binary_program(path);
        ASSERT(!saved.code);
        destroy_binary_program(&saved);
    }
    remove(path);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    return true;
}

bool test_inheritance_lifetime(void) {
    process_t *proc = create_process();
    object_t *root = get_root_object();
    collect_garbage(proc);
    size_t baseline = proc->objects.size;
    for (size_t i = 0; i < 100; i++) {
        object_t *parent = create_user_defined_object(proc, (object_array_t){&root, 1});
        object_static_string_t key_store = {0};
        object_t *key = get_static_string_object(&key_store, U"x");
        ASSERT(create_object_property(parent, key, get_static_integer_object(1), true) == MSTAT_OK);
        object_t *child = create_user_defined_object(proc, (object_array_t){&parent, 1});
        enable_prototype_shadowing(child);
        add_gc_root(proc, child);
        DECREF(parent);
        collect_garbage(proc);
        ASSERT(get_object_integer_value(get_object_property(parent, key)).value == 1);
        ASSERT(set_object_property(child, key, get_static_integer_object(2)) == MSTAT_OK);
        ASSERT(get_object_integer_value(get_object_property(parent, key)).value == 1);
        object_t *copy = clone_object(proc, child);
        ASSERT(set_object_property(copy, get_string_length(), get_integer_zero()) == MSTAT_OK);
        DECREF(copy);
        remove_gc_root(proc, child);
        DECREF(child);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
        /* A pooled ordinary object must not retain the explicit-inheritance write policy. */
        object_t *plain = create_user_defined_object(proc, (object_array_t){&root, 1});
        ASSERT(set_object_property(plain, key, get_integer_zero()) == MSTAT_PROPERTY_NOT_FOUND);
        DECREF(plain);
        collect_garbage(proc);
    }
    destroy_process(proc);
    return true;
}
