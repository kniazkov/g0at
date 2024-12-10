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

/**
 * @enum opcode_t
 * @brief Enumeration of available opcodes for the Goat virtual machine.
 *
 * Each opcode corresponds to a specific instruction that can be executed by the virtual machine.
 * These opcodes define the operations that can be performed by the virtual machine, such as 
 * arithmetic operations and control flow operations.
 */
typedef enum {
    NOP = 0x00, /**< No operation - does nothing. */
    ADD = 0x01, /**< Add operation - adds two operands from the stack. */
} opcode_t;

/**
 * @struct instruction_t
 * @brief Structure representing a single bytecode instruction.
 *
 * Each instruction in the Goat virtual machine consists of three parts: the opcode,
 * flags, and data.
 * - The `opcode` indicates the operation to be performed.
 * - The `flags` provide additional options or settings that may influence the execution of
 *   the operation.
 * - The `data` field is a union that can represent different types of operands or parameters,
 *   depending on the instruction.
 *
 * This design allows the virtual machine to handle a variety of instructions efficiently
 * and flexibly.
 */
typedef struct {
    /**
     * @brief The opcode (1 byte) specifies the type of operation to be performed.
     */
    uint8_t opcode;
    /**
     * @brief Flags (3 bytes) that modify the behavior of the instruction.
     */
    uint8_t flags[3];
    /**
     * @brief Union representing the operand or parameter for the instruction. The specific type 
              used depends on the instruction's opcode and context.
     */
    union {
        /**
         * @brief Two 32-bit addresses, typically used for instructions requiring memory
         *        or branch targets.
         */
        uint32_t address[2];
        /**
         * @brief A 64-bit signed integer, used for instructions involving integer arithmetic.
         */
        int64_t integer;
        /**
         * @brief A 64-bit floating-point value, used for instructions involving floating-point
         *        operations.
         */
        double real;
    } data;
} instruction_t;
