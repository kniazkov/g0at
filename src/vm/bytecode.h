/**
 * @file bytecode.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure and operations for bytecode in the Goat virtual machine.
 */
#pragma once

#include "common/types.h"
#include "lib/value.h"
#include "opcodes.h"

#include <stddef.h>
#include <stdint.h>

/** @brief Independent v1 wire identity, shared by standalone and module images. */
#define BINARY_FILE_SIGNATURE "GOATBC01"
#define BYTECODE_VERSION 1u
#define BYTECODE_HEADER_SIZE 64u

typedef struct {
    uint8_t opcode, flags;
    uint16_t arg0;
    uint32_t arg1;
} instruction_t;

typedef struct {
    uint32_t offset, size, kind, count;
} data_descriptor_t;

enum { DATA_STRING = 1, DATA_PARAMETERS = 2, DATA_DECLARATION = 3 };

typedef struct {
    uint8_t signature[8];
    uint32_t version, header_size, total_size;
    uint32_t instruction_count, data_count, file_count, function_count, location_count;
    uint32_t instructions_offset, data_descriptors_offset, files_offset;
    uint32_t functions_offset, locations_offset, data_offset;
} goat_binary_header_t;

typedef struct {
    uint32_t first, count, entry, reserved;
} bytecode_file_t;

typedef struct {
    uint32_t id, file, instruction, body, parameters, parameter_count, name, reserved;
} bytecode_function_t;

typedef struct {
    uint32_t first, count, file, line, column, function;
} bytecode_location_t;

_Static_assert(BSTORE == 72 && MODULE_DIR == 76 && CHECKPROP == 77 && CHECKBOUND == 80,
               "v1 opcode numbering");

/* These are checked views of an explicit wire layout, never an ABI-dependent dump. */
_Static_assert(sizeof(instruction_t) == 8 && offsetof(instruction_t, arg0) == 2
                   && offsetof(instruction_t, arg1) == 4,
               "instruction layout");
_Static_assert(sizeof(data_descriptor_t) == 16 && offsetof(data_descriptor_t, count) == 12,
               "data descriptor layout");
_Static_assert(sizeof(goat_binary_header_t) == 64
                   && offsetof(goat_binary_header_t, instructions_offset) == 40
                   && offsetof(goat_binary_header_t, data_offset) == 60,
               "header layout");
_Static_assert(sizeof(bytecode_file_t) == 16 && sizeof(bytecode_function_t) == 32
                   && sizeof(bytecode_location_t) == 24,
               "table layouts");
_Static_assert(_Alignof(instruction_t) <= 8 && _Alignof(data_descriptor_t) <= 8
                   && _Alignof(bytecode_file_t) <= 8 && _Alignof(bytecode_function_t) <= 8
                   && _Alignof(bytecode_location_t) <= 8,
               "image alignment");

typedef struct native_function_descriptor_t native_function_descriptor_t;

/** @brief File-local declaration identity is its data descriptor index, never a C address.
 * file is the local file-table index; runtime identities live outside the image.
 */
typedef struct {
    uint32_t tag;
    uint32_t name;
    uint32_t file;
    uint32_t modifier;
} declaration_descriptor_t;

#define DECLARATION_TAG 0x4445434cu

/** @brief A loaded bytecode file in memory. */
typedef struct {
    /** @brief Pointer to the entire loaded bytecode file in memory. */
    const void *buffer;

    /** @brief The buffer (file) size in bytes. */
    size_t buffer_size;

    /** @brief True only for wrappers borrowing a separately owned immutable image. */
    bool borrowed_buffer;

    /** @brief Pointer to the list of instructions (type: instruction_t*). */
    const instruction_t *instructions;

    /** @brief The number of instructions in the bytecode. */
    size_t instructions_count;

    /** @brief Pointer to the list of data descriptors (type: data_descriptor_t*). */
    const data_descriptor_t *data_descriptors;

    /** @brief The number of data descriptors in the bytecode. */
    size_t data_descriptor_count;

    /** @brief Pointer to the actual data (type: uint8_t*). */
    const uint8_t *data;

    const bytecode_file_t *files;
    const bytecode_function_t *functions;
    const bytecode_location_t *locations;
    size_t file_count, function_count, location_count;

    /** @brief Optional owned descriptors indexed by FUNC instruction; never serialized. */
    native_function_descriptor_t **native_functions;
    struct module_t *declaration_unit; /**< Runtime identity shared with declared cells. */
} bytecode_t;

bool get_bytecode_declaration(const bytecode_t *code,
                              uint32_t index,
                              declaration_descriptor_t *out);

/**
 * @brief Converts a sequence of bytecode instructions into a formatted text representation.
 *
 * The output is formatted in columns for readability, with aligned numbers, opcodes, and argument
 * values.
 */
string_value_t bytecode_to_text(const bytecode_t *code);

/** @brief Creates a runtime wrapper over a trusted, validated immutable image.
 * The source buffer must outlive the wrapper and all its image-backed objects.
 * Native bindings and declaration identity start empty and are owned separately;
 * neither mutable state nor ownership is inherited from source. NULL yields NULL.
 * Release the wrapper with free_bytecode, which does not release its borrowed image.
 */
bytecode_t *borrow_bytecode_image(const bytecode_t *source);

/** @brief Frees the runtime wrapper and its owned resources, including an owned image. */
void free_bytecode(bytecode_t *code);

/** @brief Binds or clears FUNC metadata before execution; retains the descriptor on success. */
bool bind_bytecode_native_function(bytecode_t *code,
                                   instr_index_t instruction,
                                   native_function_descriptor_t *function);
/** @brief Borrows the descriptor associated with a FUNC instruction. */
native_function_descriptor_t *get_bytecode_native_function(const bytecode_t *code,
                                                           instr_index_t instruction);

/** @brief Validates an owned aligned image and sets const views; never changes image bytes. */
bool validate_bytecode_image(bytecode_t *code);
/** @brief Copies an untrusted image once into aligned storage and validates it. */
bytecode_t *decode_bytecode_image(const void *data, size_t size);
