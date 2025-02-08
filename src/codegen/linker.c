/**
 * @file linker.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Functions implementation for linking bytecode and static data into a single binary file
 *  for the Goat virtual machine.
 */

#include <memory.h>

#include "linker.h"
#include "lib/allocate.h"

bytecode_t *link_code_and_data(code_builder_t *code_builder, data_builder_t *data_builder) {
    size_t instructions_size = code_builder->size * sizeof(instruction_t);
    size_t data_size = data_builder->data_size;
    size_t descriptors_size = data_builder->descriptors_count * sizeof(data_descriptor_t);
    size_t total_size = 
        sizeof(goat_binary_header_t) + instructions_size + descriptors_size + data_size;
    
    void *buffer = ALLOC(total_size);

    goat_binary_header_t *header = (goat_binary_header_t*)buffer;
    memcpy(header->signature, BINARY_FILE_SIGNATURE, 8);

    uint64_t instructions_offset = sizeof(goat_binary_header_t);
    uint64_t data_descriptors_offset = instructions_offset + instructions_size;
    uint64_t data_offset = data_descriptors_offset + descriptors_size;

    header->instructions_offset = instructions_offset;
    header->data_descriptors_offset = data_descriptors_offset;
    header->data_offset = data_offset;
    
    instruction_t *instructions_start = (instruction_t*)((uint8_t*)buffer + instructions_offset);
    memcpy(instructions_start, code_builder->instructions, instructions_size);
    
    data_descriptor_t *descriptors_start =
        (data_descriptor_t*)((uint8_t*)buffer + data_descriptors_offset);
    memcpy(descriptors_start, data_builder->descriptors, descriptors_size);
    
    uint8_t *data_start = (uint8_t*)buffer + data_offset;
    memcpy(data_start, data_builder->data, data_size);

    bytecode_t *result = (bytecode_t *)ALLOC(sizeof(bytecode_t));
    result->buffer = buffer;
    result->buffer_size = total_size;
    result->instructions = instructions_start;
    result->instructions_count = code_builder->size;
    result->data_descriptors = descriptors_start;
    result->data_descriptor_count = descriptors_size / sizeof(data_descriptor_t);
    result->data = data_start;
    return result;
}
