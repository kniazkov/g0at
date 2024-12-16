/**
 * @file bytecode.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure and operations for bytecode in the Goat virtual machine.
 *
 * This file contains the description of the bytecode executed by the Goat virtual machine.
 * 
 * Bytecode is a compact, low-level representation of a program, designed for efficient 
 * execution by the virtual machine. It is an intermediate form between high-level source code 
 * and machine code, optimized for portability and ease of interpretation.
 * 
 * Each instruction in the bytecode has a fixed size. This uniformity simplifies the design 
 * of the virtual machine by allowing straightforward indexing, decoding, and execution of 
 * instructions. The fixed size also ensures predictable memory usage and enables efficient 
 * looping through instructions during execution.
 * 
 * The Goat virtual machine processes bytecode in a stack-based manner, where operands 
 * are pushed to and popped from a stack, further streamlining the instruction set and 
 * reducing the complexity of the execution model.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "opcodes.h"

/**
 * @brief Signature added to the beginning of each binary file.
 */
#define BINARY_FILE_SIGNATURE "goat v.1"

#pragma pack(push, 1)

/**
 * @struct instruction_t
 * @brief Structure representing a single bytecode instruction.
 *
 * Each instruction in the Goat virtual machine consists of four fields: the opcode, flags, 
 * and two arguments (`arg0` and `arg1`).
 * - The `opcode` field specifies the operation to be performed.
 * - The `flags` field provides additional options or settings that may influence the execution 
 *   of the operation.
 * - The `arg0` field represents a 16-bit operand or parameter.
 * - The `arg1` field represents a 32-bit operand or parameter.
 *
 * This structure is packed to ensure it occupies exactly 8 bytes, making it highly efficient 
 * for use in memory-constrained environments and ensuring compatibility across different platforms.
 */
typedef struct {
    /**
     * @brief The opcode (1 byte) specifies the type of operation to be performed.
     */
    uint8_t opcode;

    /**
     * @brief Flags (1 byte) that modify the behavior of the instruction.
     */
    uint8_t flags;

    /**
     * @brief First argument (16 bits, 2 bytes).
     */
    uint16_t arg0;

    /**
     * @brief Second argument (32 bits, 4 bytes).
     */
    uint32_t arg1;
} instruction_t;

#pragma pack(pop)

#pragma pack(push, 4)

/**
 * @struct data_descriptor_t
 * @brief Structure representing a descriptor that addresses data within a segment.
 *
 * This structure describes the location and size of a data block within a data segment.
 * - The `offset` specifies the offset (in bytes) from the beginning of the data segment
 *   to the start of the data block.
 * - The `size` specifies the size (in bytes) of the data block.
 *
 * The structure is packed to ensure it occupies exactly 12 bytes with 4-byte alignment.
 */
typedef struct {
    /**
     * @brief Offset (8 bytes) from the beginning of the data segment to the start
     *  of the data block.
     */
    uint64_t offset;

    /**
     * @brief Size (4 bytes) of the data block.
     */
    uint32_t size;
} data_descriptor_t;

#pragma pack(pop)

#pragma pack(push, 8)

/**
 * @struct goat_binary_header_t
 * @brief Structure representing the header of the Goat binary file.
 *
 * This structure describes the layout of a binary file containing bytecode for the
 * Goat virtual machine. It includes offsets to various sections within the file, such as
 * the list of instructions and data descriptors.
 */
typedef struct {
    /**
     * @brief 8-byte signature that uniquely identifies the file format.
     */
    uint8_t signature[8];

    /**
     * @brief 8-byte offset from the beginning of the file to the list of instructions.
     */
    uint64_t instructions_offset;

    /**
     * @brief 8-byte offset from the beginning of the file to the list of data descriptors.
     */
    uint64_t data_descriptors_offset;

    /**
     * @brief 8-byte offset from the beginning of the file to the actual data.
     */
    uint64_t data_offset;
} goat_binary_header_t;

#pragma pack(pop)

/**
 * @struct bytecode_t
 * @brief Structure representing a loaded bytecode file in memory.
 *
 * This structure holds the addresses for various parts of the loaded bytecode file.
 * It includes pointers to the bytecode itself, the list of instructions, the list
 * of data descriptors, and the actual data, allowing easy access by index.
 */
typedef struct {
    /**
     * @brief Pointer to the entire loaded bytecode file in memory.
     * 
     * This is a void pointer to avoid type-casting.
     * Used for memory cleanup at the end of execution.
     */
    void *buffer;

    /**
     * @brief The buffer (file) size in bytes.
     * 
     * Used when writing a buffer to a file.
     */
    size_t buffer_size;

    /**
     * @brief Pointer to the list of instructions (type: instruction_t*).
     * 
     * Points to the first instruction in the bytecode file.
     */
    instruction_t *instructions;

    /**
     * @brief Pointer to the list of data descriptors (type: data_descriptor_t*).
     * 
     * Points to the first data descriptor in the bytecode file.
     */
    data_descriptor_t *data_descriptors;

    /**
     * @brief The number of data descriptors in the bytecode.
     */
    size_t data_descriptor_count;

    /**
     * @brief Pointer to the actual data (type: uint8_t*).
     * 
     * Points to the first byte of the data section in the bytecode file.
     */
    uint8_t *data;
} bytecode_t;

/**
 * @brief Frees the memory allocated by the bytecode structure.
 *
 * This function frees the memory used by the `bytecode_t` structure, which includes the entire
 * binary file (instructions, data, and descriptors).
 *
 * @param code A pointer to the `bytecode_t` structure to be freed.
 */
void free_bytecode(bytecode_t *code);
