/**
 * @file code_builder.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures and functions for building and managing a list of bytecode
 * instructions.
 */

#pragma once

#include "common/types.h"
#include "vm/bytecode.h"

#include <stddef.h>
#include <stdint.h>

typedef struct code_builder_t code_builder_t;

/** @brief Building a list of instructions in a bytecode file. */
struct code_builder_t {
    /**
     * @brief Pointer to the list of instructions. This is dynamically allocated and resized as new
     * instructions are added.
     */
    instruction_t *instructions;

    /** @brief The current number of instructions in the list. */
    size_t size;

    /**
     * @brief The maximum capacity of the instructions list. If the size exceeds this value, the
     * list will be resized.
     */
    size_t capacity;
};

/** @brief Creates a new code builder with a default initial capacity. */
code_builder_t *create_code_builder();

/** @brief Adds a new instruction to the builder's list of instructions. */
instr_index_t add_instruction(code_builder_t *builder, instruction_t instruction);

/**
 * @brief Returns an instruction for patching; subsequent growth may invalidate the pointer.
 * `index`: Index of an existing instruction.
 */
static inline instruction_t *get_instruction(code_builder_t *builder, instr_index_t index) {
    return &builder->instructions[index];
}

/** @brief Returns the index that will be assigned to the next added instruction. */
static inline instr_index_t get_next_instruction_index(const code_builder_t *builder) {
    return (instr_index_t)builder->size;
}

/**
 * @brief Destroys the code builder and frees its memory.
 *
 * After calling this function, the builder should no longer be used.
 */
void destroy_code_builder(code_builder_t *builder);
