/**
 * @file code_builder.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines structures and functions for building and managing a list of
 *  bytecode instructions.
 *
 * This file contains the declarations for a set of structures and functions that help in building
 * and managing the list of bytecode instructions. It provides mechanisms to add, modify, and
 * retrieve instructions in a flexible and efficient manner.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "vm/bytecode.h"

/**
 * @typedef code_builder_t
 * @brief Forward declaration for the code builder structure.
 */
typedef struct code_builder_t code_builder_t;

/**
 * @struct code_builder_t
 * @brief Structure for building a list of instructions in a bytecode file.
 *
 * This structure holds the list of instructions and provides the necessary operations
 * to add new instructions and manage their memory. The list is dynamically resized as
 * new instructions are added.
 */
struct code_builder_t {
    /**
     * @brief Pointer to the list of instructions.
     *  This is dynamically allocated and resized as new instructions are added.
     */
    instruction_t *instructions;

    /**
     * @brief The current number of instructions in the list.
     */
    size_t size;

    /**
     * @brief The maximum capacity of the instructions list.
     *  If the size exceeds this value, the list will be resized.
     */
    size_t capacity;
};

/**
 * @brief Creates a new code builder with a default initial capacity.
 *
 * This function initializes a new code builder with a fixed initial capacity.
 * The list of instructions is allocated, and the builder is ready to add new instructions.
 *
 * @return A pointer to the created code builder.
 */
code_builder_t *create_code_builder();

/**
 * @brief Adds a new instruction to the builder's list of instructions.
 *
 * This function adds a new instruction to the list. If the list has reached its capacity,
 * it will automatically resize the list to accommodate more instructions.
 * The function returns a pointer to the added instruction, allowing it to be modified later.
 *
 * @param builder The code builder to which the instruction will be added.
 * @param instruction The instruction to be added.
 * @return A pointer to the added instruction.
 */
instruction_t *add_instruction(code_builder_t *builder, instruction_t instruction);

/**
 * @brief Destroys the code builder and frees its memory.
 *
 * This function frees the memory used by the instruction list and the builder itself.
 * After calling this function, the builder should no longer be used.
 *
 * @param builder The code builder to be destroyed.
 */
void destroy_code_builder(code_builder_t *builder);
