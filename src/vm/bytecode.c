/**
 * @file bytecode.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements functions for managing bytecode.
 */

#include "bytecode.h"

#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/access.h"
#include "model/native_library.h"

#include <stdbool.h>
#include <string.h>

/** @brief Describes the properties of a bytecode instruction. */
typedef struct {
    const gchar_t *code; /**< The opcode name as a wide-character string. */
    bool has_flags;      /**< Indicates if the instruction has flag bits. */
    bool has_arg_0;      /**< Indicates if the instruction has a first argument (`arg0`). */
    bool arg_1_is_unsigned_integer; /**< Indicates if `arg1` is an unsigned integer. */
    bool arg_1_is_signed_integer;   /**< Indicates if `arg1` is a signed integer. */
    bool arg_1_is_string;           /**< Indicates if `arg1` is a string reference. */
} instruction_descriptor_t;

/** @brief An array of instruction descriptors defining the available bytecode instructions. */
static instruction_descriptor_t descriptors[] = {
    {.code = U"NOP"},
    {.code = U"ARG", .arg_1_is_unsigned_integer = true},
    {.code = U"END"},
    {.code = U"JUMP", .arg_1_is_unsigned_integer = true},
    {.code = U"JIF", .arg_1_is_unsigned_integer = true},
    {.code = U"POP"},
    {.code = U"DUP"},
    {.code = U"NIL"},
    {.code = U"TRUE"},
    {.code = U"FALSE"},
    {.code = U"ILOAD32", .arg_1_is_signed_integer = true},
    {.code = U"ILOAD64", .arg_1_is_unsigned_integer = true},
    {.code = U"RLOAD", .arg_1_is_unsigned_integer = true},
    {.code = U"SLOAD", .arg_1_is_string = true},
    {.code = U"CLOAD", .arg_1_is_unsigned_integer = true},
    {.code = U"VLOAD", .arg_1_is_string = true},
    {.code = U"VAR", .arg_1_is_string = true},
    {.code = U"CONST", .arg_1_is_string = true},
    {.code = U"STORE", .arg_1_is_string = true},
    {.code = U"GETPROP", .arg_1_is_string = true},
    {.code = U"SETPROP", .arg_1_is_string = true},
    {.code = U"UPLUS"},
    {.code = U"UMINUS"},
    {.code = U"INC"},
    {.code = U"DEC"},
    {.code = U"LNOT"},
    {.code = U"BOOL"},
    {.code = U"BNOT"},
    {.code = U"LAND", .arg_1_is_unsigned_integer = true},
    {.code = U"LOR", .arg_1_is_unsigned_integer = true},
    {.code = U"BAND"},
    {.code = U"BOR"},
    {.code = U"BXOR"},
    {.code = U"SHL"},
    {.code = U"SHR"},
    {.code = U"ADD"},
    {.code = U"SUB"},
    {.code = U"MUL"},
    {.code = U"DIVIDE"},
    {.code = U"MODULO"},
    {.code = U"POWER"},
    {.code = U"LESS"},
    {.code = U"LEQ"},
    {.code = U"GREATER"},
    {.code = U"GREQ"},
    {.code = U"EQUAL"},
    {.code = U"DIFF"},
    {.code = U"FUNC", .has_arg_0 = true, .arg_1_is_unsigned_integer = true},
    {.code = U"CALL", .has_arg_0 = true},
    {.code = U"CALLM", .has_arg_0 = true},
    {.code = U"RET"},
    {.code = U"ENTER"},
    {.code = U"LEAVE"},
    {.code = U"RESTORE"},
    {.code = U"TRY", .arg_1_is_unsigned_integer = true},
    {.code = U"THROW"},
    {.code = U"GETINDEX", .arg_1_is_unsigned_integer = true},
    {.code = U"SETINDEX", .arg_1_is_unsigned_integer = true},
    {.code = U"ARRAY", .arg_1_is_unsigned_integer = true},
    {.code = U"INHERIT", .has_arg_0 = true, .arg_1_is_unsigned_integer = true},
    {.code = U"LOOP_ENTER"},
    {.code = U"LOOP_JUMP", .arg_1_is_unsigned_integer = true},
    {.code = U"DUPN", .arg_1_is_unsigned_integer = true},
    {.code = U"ROTATE", .arg_1_is_unsigned_integer = true},
    {.code = U"SNAPSHOT"},
    {.code = U"ITER_NEXT", .arg_1_is_unsigned_integer = true},
    {.code = U"FINALLY_ENTER", .arg_1_is_unsigned_integer = true},
    {.code = U"FINALLY_LEAVE", .arg_1_is_unsigned_integer = true},
    {.code = U"FINALLY_END"},
    {.code = U"DVAR", .arg_1_is_unsigned_integer = true},
    {.code = U"DCONST", .arg_1_is_unsigned_integer = true},
    {.code = U"BLOAD", .arg_1_is_unsigned_integer = true},
    {.code = U"BSTORE", .arg_1_is_unsigned_integer = true},
    {.code = U"INCLUDE", .arg_1_is_unsigned_integer = true},
    {.code = U"IMPORT", .arg_1_is_string = true},
    {.code = U"MAIN"},
    {.code = U"DIR"},
    {.code = U"CHECKPROP", .arg_1_is_string = true},
    {.code = U"CHECKINDEX", .arg_1_is_unsigned_integer = true},
    {.code = U"CHECKVAR", .arg_1_is_string = true},
    {.code = U"CHECKBOUND", .arg_1_is_unsigned_integer = true}};

/** @brief Defines the column width for instruction numbers in the bytecode text representation. */
#define NUMBER_COLUMN_SIZE 8

/** @brief Defines the column width for opcodes in the bytecode text representation. */
#define OPCODE_COLUMN_SIZE 11

/** @brief Defines the column width for flag bits in the bytecode text representation. */
#define FLAGS_COLUMN_SIZE 9

/** @brief Defines the column width for the first argument (arg0) in the bytecode text
 * representation. */
#define ARG0_COLUMN_SIZE 6

/** @brief Defines the column width for the second argument (arg1) in the bytecode text
 * representation. */
#define ARG1_COLUMN_SIZE 12

string_value_t bytecode_to_text(const bytecode_t *code) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t index = 0; index < code->instructions_count; index++) {
        instruction_t instr = code->instructions[index];
        instruction_descriptor_t descr = descriptors[instr.opcode];

        // 1. number
        if (index % 5 == 0) {
            string_value_t number = format_string(U"%zu", index);
            string_value_t aligned = align_text(number, NUMBER_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, U' ');
            FREE_STRING(aligned);
            FREE_STRING(number);
        } else {
            append_repeated_char(&builder, U' ', NUMBER_COLUMN_SIZE);
        }

        // 2. opcode
        size_t opcode_size = STRLEN(descr.code);
        append_substring(&builder, descr.code, opcode_size);
        append_repeated_char(&builder, U' ', OPCODE_COLUMN_SIZE - opcode_size - 1);

        // 3. flags
        if (descr.has_flags) {
            for (int bit = 7; bit >= 0; bit--) {
                append_char(&builder, instr.flags | 1 << bit ? U'1' : U'0');
            }
            append_char(&builder, U' ');
        } else {
            append_repeated_char(&builder, U' ', FLAGS_COLUMN_SIZE);
        }

        // 4. arg0
        if (descr.has_arg_0) {
            string_value_t value = format_string(U"%zu", instr.arg0);
            string_value_t aligned = align_text(value, ARG0_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, U' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
        } else {
            append_repeated_char(&builder, U' ', ARG0_COLUMN_SIZE);
        }

        // 5. arg1
        if (descr.arg_1_is_unsigned_integer || descr.arg_1_is_string) {
            string_value_t value = format_string(U"%zu", instr.arg1);
            string_value_t aligned = align_text(value, ARG1_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, U' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
            if (descr.arg_1_is_string) {
                data_descriptor_t data = code->data_descriptors[instr.arg1];
                string_value_t str = string_to_string_notation(
                    U"",
                    (string_value_t){(const gchar_t *)(code->data + data.offset),
                                     data.size / sizeof(gchar_t) - 1,
                                     false});
                append_char(&builder, U' ');
                append_string_value(&builder, str);
                FREE_STRING(str);
            }
        } else if (descr.arg_1_is_signed_integer) {
            string_value_t value = format_string(U"%i", instr.arg1);
            string_value_t aligned = align_text(value, ARG1_COLUMN_SIZE - 1, ALIGN_RIGHT);
            append_string_value(&builder, aligned);
            append_char(&builder, U' ');
            FREE_STRING(aligned);
            FREE_STRING(value);
        }

        append_char(&builder, U'\n');
    }
    return append_char(&builder, U'\n');
}

bytecode_t *borrow_bytecode_image(const bytecode_t *source) {
    if (!source)
        return NULL;
    bytecode_t *code = ALLOC(sizeof(*code));
    *code = *source;
    code->borrowed_buffer = true;
    code->native_functions = NULL;
    code->declaration_unit = NULL;
    return code;
}

void free_bytecode(bytecode_t *code) {
    release_module_identity(code->declaration_unit);
    if (code->native_functions) {
        for (size_t i = 0; i < code->instructions_count; i++)
            release_native_function_descriptor(code->native_functions[i]);
        FREE(code->native_functions);
    }
    if (!code->borrowed_buffer)
        FREE((void *)code->buffer);
    FREE(code);
}

bool get_bytecode_declaration(const bytecode_t *code,
                              uint32_t index,
                              declaration_descriptor_t *out) {
    if (index >= code->data_descriptor_count)
        return false;
    data_descriptor_t d = code->data_descriptors[index];
    size_t prefix = (size_t)(code->data - (const uint8_t *)code->buffer);
    if (d.kind != DATA_DECLARATION || prefix > code->buffer_size
        || d.offset > code->buffer_size - prefix || d.size != sizeof(*out)
        || d.size > code->buffer_size - prefix - d.offset)
        return false;
    memcpy(out, code->data + d.offset, sizeof(*out));
    return out->tag == DECLARATION_TAG && out->file < code->file_count
           && out->modifier <= ACCESS_PRIVATE && out->name < code->data_descriptor_count;
}

bool bind_bytecode_native_function(bytecode_t *code,
                                   instr_index_t instruction,
                                   native_function_descriptor_t *function) {
    if (!code || instruction >= code->instructions_count
        || code->instructions[instruction].opcode != FUNC)
        return false;
    for (uint32_t i = 0; i < get_native_function_entry_count(function); i++)
        if (get_native_function_entry(function, i)->parameter_count
            != code->instructions[instruction].arg0)
            return false;
    if (!code->native_functions) {
        if (!function)
            return true;
        code->native_functions = CALLOC(
            allocation_size_multiply(code->instructions_count, sizeof(*code->native_functions)));
    }
    retain_native_function_descriptor(function);
    release_native_function_descriptor(code->native_functions[instruction]);
    code->native_functions[instruction] = function;
    return true;
}

native_function_descriptor_t *get_bytecode_native_function(const bytecode_t *code,
                                                           instr_index_t instruction) {
    return code && code->native_functions && instruction < code->instructions_count
               ? code->native_functions[instruction]
               : NULL;
}
