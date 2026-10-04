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

/** @brief Signature added to the beginning of each binary file. */
#define BINARY_FILE_SIGNATURE "goat v.1"

#pragma pack(push, 1)

/** @brief A single bytecode instruction. */
typedef struct {
    /** @brief The opcode (1 byte) specifies the type of operation to be performed. */
    uint8_t opcode;

    /** @brief Flags (1 byte) that modify the behavior of the instruction. */
    uint8_t flags;

    /** @brief First argument (16 bits, 2 bytes). */
    uint16_t arg0;

    /** @brief Second argument (32 bits, 4 bytes). */
    uint32_t arg1;
} instruction_t;

#pragma pack(pop)

#pragma pack(push, 4)

/**
 * @brief A descriptor that addresses data within a segment.
 *
 * The structure is packed to ensure it occupies exactly 12 bytes with 4-byte alignment.
 */
typedef struct {
    /** @brief Offset (8 bytes) from the beginning of the data segment to the start of the data
     * block. */
    uint64_t offset;

    /** @brief Size (4 bytes) of the data block. */
    uint32_t size;
} data_descriptor_t;

#pragma pack(pop)

#pragma pack(push, 8)

/** @brief The header of the Goat binary file. */
typedef struct {
    /** @brief 8-byte signature that uniquely identifies the file format. */
    uint8_t signature[8];

    /** @brief 8-byte offset from the beginning of the file to the list of instructions. */
    uint64_t instructions_offset;

    /** @brief 8-byte offset from the beginning of the file to the list of data descriptors. */
    uint64_t data_descriptors_offset;

    /** @brief 8-byte offset from the beginning of the file to the actual data. */
    uint64_t data_offset;
} goat_binary_header_t;

#pragma pack(pop)

typedef struct native_function_descriptor_t native_function_descriptor_t;

/** @brief A loaded bytecode file in memory. */
typedef struct {
    /** @brief Pointer to the entire loaded bytecode file in memory. */
    void *buffer;

    /** @brief The buffer (file) size in bytes. */
    size_t buffer_size;

    /** @brief Pointer to the list of instructions (type: instruction_t*). */
    instruction_t *instructions;

    /** @brief The number of instructions in the bytecode. */
    size_t instructions_count;

    /** @brief Pointer to the list of data descriptors (type: data_descriptor_t*). */
    data_descriptor_t *data_descriptors;

    /** @brief The number of data descriptors in the bytecode. */
    size_t data_descriptor_count;

    /** @brief Pointer to the actual data (type: uint8_t*). */
    uint8_t *data;

    /** @brief Optional owned descriptors indexed by FUNC instruction; never serialized. */
    native_function_descriptor_t **native_functions;
} bytecode_t;

/**
 * @brief Converts a sequence of bytecode instructions into a formatted text representation.
 *
 * The output is formatted in columns for readability, with aligned numbers, opcodes, and argument
 * values.
 */
string_value_t bytecode_to_text(const bytecode_t *code);

/** @brief Frees the memory allocated by the bytecode structure. */
void free_bytecode(bytecode_t *code);

/** @brief Binds or clears FUNC metadata before execution; retains the descriptor on success. */
bool bind_bytecode_native_function(bytecode_t *code,
                                   instr_index_t instruction,
                                   native_function_descriptor_t *function);
/** @brief Borrows the descriptor associated with a FUNC instruction. */
native_function_descriptor_t *get_bytecode_native_function(const bytecode_t *code,
                                                           instr_index_t instruction);
