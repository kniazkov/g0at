/**
 * @file code_builder.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements functions for building and managing a list of bytecode instructions.
 */

#include "code_builder.h"

#include "lib/allocate.h"

#include <stdlib.h>

/** @brief Initial capacity for the instruction list */
#define INITIAL_CAPACITY 128

code_builder_t *create_code_builder(void) {
    code_builder_t *builder = (code_builder_t *)CALLOC(sizeof(code_builder_t));
    builder->size = 0;
    builder->capacity = INITIAL_CAPACITY;
    builder->instructions = (instruction_t *)ALLOC(builder->capacity * sizeof(instruction_t));
    builder->locations = CALLOC(builder->capacity * sizeof(*builder->locations));
    builder->function_ids = CALLOC(builder->capacity * sizeof(*builder->function_ids));
    return builder;
}

instr_index_t add_instruction(code_builder_t *builder, instruction_t instruction) {
    if (builder->size >= UINT32_MAX / sizeof(instruction_t))
        abort();
    if (builder->size >= builder->capacity) {
        if (builder->capacity > SIZE_MAX / 2 / sizeof(bytecode_location_t))
            abort();
        builder->capacity *= 2;
        instruction_t *new_instructions =
            (instruction_t *)ALLOC(builder->capacity * sizeof(instruction_t));
        for (size_t index = 0; index < builder->size; ++index) {
            new_instructions[index] = builder->instructions[index];
        }
        bytecode_location_t *locations = CALLOC(builder->capacity * sizeof(*locations));
        uint32_t *ids = CALLOC(builder->capacity * sizeof(*ids));
        for (size_t i = 0; i < builder->size; ++i) {
            locations[i] = builder->locations[i];
            ids[i] = builder->function_ids[i];
        }
        FREE(builder->locations);
        FREE(builder->function_ids);
        builder->locations = locations;
        builder->function_ids = ids;
        FREE(builder->instructions);
        builder->instructions = new_instructions;
    }
    builder->instructions[builder->size] = instruction;
    builder->locations[builder->size] = builder->location;
    return builder->size++;
}

void destroy_code_builder(code_builder_t *builder) {
    FREE(builder->locations);
    FREE(builder->function_ids);
    FREE(builder->instructions);
    FREE(builder);
}
