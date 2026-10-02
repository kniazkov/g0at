/**
 * @file test_codegen.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing code generator.
 */

#include "test_codegen.h"

#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/expression.h"
#include "lib/arena.h"
#include "test_macro.h"

#include <memory.h>
#include <stdio.h>

bool test_data_builder() {
    data_builder_t *builder = create_data_builder();
    uint32_t index = add_string_to_data_segment(builder, L"alpha");
    ASSERT(index == 0);
    index = add_string_to_data_segment(builder, L"beta");
    ASSERT(index == 1);
    index = add_string_to_data_segment(builder, L"gamma");
    ASSERT(index == 2);
    index = add_string_to_data_segment(builder, L"alpha");
    ASSERT(index == 0);
    ASSERT(builder->data_size % 4 == 0);
    ASSERT(builder->descriptors[1].size == sizeof(wchar_t) * 5);
    ASSERT(memcmp(builder->data + builder->descriptors[1].offset, L"beta", sizeof(wchar_t) * 5)
           == 0);
    ASSERT(memcmp(builder->data + builder->descriptors[2].offset, L"gamma", sizeof(wchar_t) * 6)
           == 0);
    /* Cross repeated buffer growth and look up old strings after every relocation. */
    for (size_t i = 0; i < 1000; i++) {
        wchar_t text[40];
        swprintf(text, 40, L"new-string-%zu", i);
        uint32_t added = add_string_to_data_segment(builder, text);
        ASSERT(added == i + 3);
        ASSERT(add_string_to_data_segment(builder, L"alpha") == 0);
        ASSERT(add_string_to_data_segment_ex(builder, (string_view_t){text, wcslen(text)})
               == added);
        ASSERT(!wcscmp((wchar_t *)(builder->data + builder->descriptors[added].offset), text));
    }
    destroy_data_builder(builder);
    return true;
}

bool test_linker() {
    code_builder_t *code_builder = create_code_builder();
    add_instruction(code_builder, (instruction_t){.opcode = ILOAD32, .arg1 = 1024});
    add_instruction(code_builder, (instruction_t){.opcode = POP});
    add_instruction(code_builder, (instruction_t){.opcode = END});
    data_builder_t *data_builder = create_data_builder();
    add_string_to_data_segment(data_builder, L"abc");
    add_string_to_data_segment(data_builder, L"0123456789");
    bytecode_t *code = link_code_and_data(code_builder, data_builder);
    destroy_code_builder(code_builder);
    destroy_data_builder(data_builder);
    ASSERT(code->instructions[2].opcode == END);
    ASSERT(memcmp(code->data + code->data_descriptors[1].offset,
                  L"0123456789",
                  code->data_descriptors[1].size)
           == 0);
    free_bytecode(code);
    return true;
}

bool test_node_codegen_stubs() {
    arena_t *arena = create_arena(8);
    node_t *node = create_integer_node(arena, 42);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    source_builder_t *source = create_source_builder();
    add_instruction(code, (instruction_t){.opcode = END});
    add_string_to_data_segment(data, L"sentinel");
    size_t data_size = data->data_size;
    add_static_source(source, 1, L"sentinel");

    ASSERT(can_generate_c_code_from_node(node, NULL));
    string_value_t result = generate_c_code_from_node(node);
    ASSERT(result.data == NULL && result.length == 0 && !result.should_free);
    generate_indented_c_code_from_node(node, source, 2);
    ASSERT(source->count == 1 && source->lines[0].indent == 1);
    ASSERT(wcscmp(source->lines[0].text.data, L"sentinel") == 0);
    ASSERT(generate_bytecode_assign_from_node(node, code, data) == BAD_INSTR_INDEX);
    ASSERT(generate_deferred_bytecode_from_node(node, code, data));
    ASSERT(code->size == 1 && code->instructions[0].opcode == END);
    ASSERT(data->descriptors_count == 1 && data->data_size == data_size);
    ASSERT(wcscmp((wchar_t *)(data->data + data->descriptors[0].offset), L"sentinel") == 0);

    destroy_source_builder(source);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_arena(arena);
    return true;
}
