/**
 * @file code_builder.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements functions for building and managing a list of bytecode instructions.
 *
 * This file provides the implementation for the `code_builder_t` structure
 * and its associated functions for dynamically building and managing a list of bytecode
 * instructions. It includes the ability to create, add instructions, resize the list,
 * and free allocated memory.
 */

#include "code_builder.h"
#include "lib/allocate.h"

/**
 * @brief Initial capacity for the instruction list
 */
#define INITIAL_CAPACITY 128

code_builder_t *create_code_builder(void) {
    code_builder_t *builder = (code_builder_t *)ALLOC(sizeof(code_builder_t));
    builder->size = 0;
    builder->capacity = INITIAL_CAPACITY;
    builder->instructions = (instruction_t *)ALLOC(builder->capacity * sizeof(instruction_t));
    return builder;
}

instruction_t *add_instruction(code_builder_t *builder, instruction_t instruction) {
    if (builder->size >= builder->capacity) {
        builder->capacity *= 2;
        instruction_t *new_instructions =
            (instruction_t *)ALLOC(builder->capacity * sizeof(instruction_t));
        for (size_t index = 0; index < builder->size; ++index) {
            new_instructions[index] = builder->instructions[index];
        }
        FREE(builder->instructions);
        builder->instructions = new_instructions;
    }
    builder->instructions[builder->size] = instruction;
    return &builder->instructions[builder->size++];
}

void destroy_code_builder(code_builder_t *builder) {
    FREE(builder->instructions);
    FREE(builder);
}
