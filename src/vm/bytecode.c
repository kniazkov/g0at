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

/**
 * @brief Describes the properties of a bytecode instruction.
 * 
 * This structure contains metadata about a specific bytecode instruction,
 * including its mnemonic code and information about its operands.
 */
typedef struct {
    const wchar_t *code; /**< The opcode name as a wide-character string. */
    bool has_flags; /**< Indicates if the instruction has flag bits. */
    bool has_arg_0; /**< Indicates if the instruction has a first argument (`arg0`). */
    bool arg_1_is_unsigned_integer; /**< Indicates if `arg1` is an unsigned integer. */
    bool arg_1_is_signed_integer; /**< Indicates if `arg1` is a signed integer. */
    bool arg_1_is_string; /**< Indicates if `arg1` is a string reference. */
} instruction_descriptor_t;

/**
 * @brief An array of instruction descriptors defining the available bytecode instructions.
 * 
 * This static array provides metadata for each instruction in the bytecode system,
 * including its opcode name and operand properties.
 */
static instruction_descriptor_t descriptors[] =
{
      { .code = L"NOP" }
    , { .code = L"ARG", .arg_1_is_unsigned_integer = true }
    , { .code = L"END" }
    , { .code = L"POP" }
    , { .code = L"NIL" }
    , { .code = L"ILOAD32", .arg_1_is_signed_integer = true }
    , { .code = L"ILOAD64", .arg_1_is_unsigned_integer = true }
    , { .code = L"SLOAD", .arg_1_is_string = true }
    , { .code = L"VLOAD", .arg_1_is_string = true }
    , { .code = L"VAR", .arg_1_is_string = true }
    , { .code = L"CONST", .arg_1_is_string = true }
    , { .code = L"STORE", .arg_1_is_string = true }
    , { .code = L"ADD" }
    , { .code = L"SUB" }
    , { .code = L"CALL", .has_arg_0 = true }
};

/**
 * @brief Defines the column width for instruction numbers in the bytecode text representation.
 */
#define NUMBER_COLUMN_SIZE 8

/**
 * @brief Defines the column width for opcodes in the bytecode text representation.
 */
#define OPCODE_COLUMN_SIZE 11

/**
 * @brief Defines the column width for flag bits in the bytecode text representation.
 */
#define FLAGS_COLUMN_SIZE 9

/**
 * @brief Defines the column width for the first argument (arg0) in the bytecode
 *  text representation.
 */
#define ARG0_COLUMN_SIZE 6

/**
 * @brief Defines the column width for the second argument (arg1) in the bytecode
 *  text representation.
 */
#define ARG1_COLUMN_SIZE 12

string_value_t bytecode_to_text(const bytecode_t *code) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t index = 0; index < code->instructions_count; index++) {
        instruction_t instr = code->instructions[index];
        instruction_descriptor_t descr = descriptors[instr.opcode];

        // 1. number
        if (index % 5 == 0) {
            string_value_t number = format_string(L"%zu", index);
            string_value_t aligned = align_text(number, NUMBER_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, L' ');
            FREE_STRING(aligned);
            FREE_STRING(number);
        } else {
            append_repeated_char(&builder, L' ', NUMBER_COLUMN_SIZE);
        }

        // 2. opcode
        size_t opcode_size = wcslen(descr.code);
        append_substring(&builder, descr.code, opcode_size);
        append_repeated_char(&builder, L' ', OPCODE_COLUMN_SIZE - opcode_size - 1);

        // 3. flags
        if (descr.has_flags) {
            for (int bit = 7; bit >= 0; bit--) {
                append_char(&builder, instr.flags | 1 << bit ? L'1' : L'0');
            }
            append_char(&builder, L' ');
        } else {
            append_repeated_char(&builder, L' ', FLAGS_COLUMN_SIZE);
        }

        // 4. arg0
        if (descr.has_arg_0) {
            string_value_t value = format_string(L"%zu", instr.arg0);
            string_value_t aligned = align_text(value, ARG0_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, L' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
        } else {
            append_repeated_char(&builder, L' ', ARG0_COLUMN_SIZE);
        }

        // 5. arg1
        if (descr.arg_1_is_unsigned_integer || descr.arg_1_is_string) {
            string_value_t value = format_string(L"%zu", instr.arg1);
            string_value_t aligned = align_text(value, ARG1_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, L' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
            if (descr.arg_1_is_string) {
                data_descriptor_t data = code->data_descriptors[instr.arg1];
                string_value_t str = string_to_string_notation(
                    L"",
                    (string_value_t) {
                        (wchar_t*)(code->data + data.offset),
                        data.size / sizeof(wchar_t) - 1,
                        false
                    }
                );
                append_char(&builder, L' ');
                append_string_value(&builder, str);
                FREE_STRING(str);
            }
        }
        else if (descr.arg_1_is_signed_integer) {
            string_value_t value = format_string(L"%i", instr.arg1);
            string_value_t aligned = align_text(value, ARG1_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, L' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
        }

        append_char(&builder, L'\n');
    }
    return append_char(&builder, L'\n');
}

void free_bytecode(bytecode_t *code) {
    FREE(code->buffer);
    FREE(code);
}
