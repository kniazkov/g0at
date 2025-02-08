/**
 * @file bytecode.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements functions for managing bytecode.
 */

#include <stdbool.h>
#include <string.h>

#include "bytecode.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

typedef struct {
    const wchar_t *code;
    bool has_flags;
    bool has_arg_0;
    bool arg_1_is_unsigned_integer;
    bool arg_1_is_signed_integer;
    bool arg_1_is_string;
} instruction_descriptor_t;

static instruction_descriptor_t descriptors[] =
{
      { .code = L"NOP" }
    , { .code = L"ARG" }
    , { .code = L"END" }
    , { .code = L"POP" }
    , { .code = L"ILOAD32" }
    , { .code = L"ILOAD64" }
    , { .code = L"SLOAD", .arg_1_is_string = true }
    , { .code = L"VLOAD", .arg_1_is_string = true }
    , { .code = L"STORE" }
    , { .code = L"ADD" }
    , { .code = L"SUB" }
    , { .code = L"CALL" }
};

#define OPCODE_COLUMN_SIZE 10

string_value_t bytecode_to_text(const bytecode_t *code) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t index = 0; index < code->instructions_count; index++) {
        instruction_t instr = code->instructions[index];
        instruction_descriptor_t descr = descriptors[instr.opcode];

        // 1. opcode
        size_t opcode_size = wcslen(descr.code);
        append_substring(&builder, descr.code, opcode_size);
        for (size_t space = opcode_size; space < OPCODE_COLUMN_SIZE; space++) {
            append_char(&builder, L' ');
        }

        append_char(&builder, L'\n');
    }
    return append_char(&builder, L'\n');
}

void free_bytecode(bytecode_t *code) {
    FREE(code->buffer);
    FREE(code);
}
