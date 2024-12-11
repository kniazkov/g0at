/**
 * @file test_codegen.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing code generator.
 */

#include <stdio.h>
#include <memory.h>

#include "test_codegen.h"
#include "test_macro.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/linker.h"

bool test_data_builder() {
    data_builder_t *builder = data_builder_create();
    uint32_t index = data_builder_add_string(builder, L"alpha");
    ASSERT(index == 0);
    index = data_builder_add_string(builder, L"beta");
    ASSERT(index == 1);
    index = data_builder_add_string(builder, L"gamma");
    ASSERT(index == 2);
    index = data_builder_add_string(builder, L"alpha");
    ASSERT(index == 0);
    ASSERT(builder->data_size % 4 == 0);
    ASSERT(builder->descriptors[1].size == sizeof(wchar_t) * 5);
    ASSERT(
        memcmp(builder->data + builder->descriptors[1].offset, L"beta", sizeof(wchar_t) * 5) == 0
    );
    ASSERT(
        memcmp(builder->data + builder->descriptors[2].offset, L"gamma", sizeof(wchar_t) * 6) == 0
    );
    data_builder_destroy(builder);
    return true;
}

bool test_linker() {
    code_builder_t *code_builder = code_builder_create();
    code_builder_add(code_builder, (instruction_t){ .opcode = ILOAD32, .arg1 = 1024 });
    code_builder_add(code_builder, (instruction_t){ .opcode = POP });
    code_builder_add(code_builder, (instruction_t){ .opcode = END });
    data_builder_t *data_builder = data_builder_create();
    data_builder_add_string(data_builder, L"abc");
    data_builder_add_string(data_builder, L"0123456789");
    bytecode_t *code = link_code_and_data(code_builder, data_builder);
    code_builder_destroy(code_builder);
    data_builder_destroy(data_builder);
    ASSERT(code->instructions[2].opcode == END);
    ASSERT(
        memcmp(code->data + code->data_descriptors[1].offset, L"0123456789",
            code->data_descriptors[1].size) == 0
    );
    free_bytecode(code);
}
