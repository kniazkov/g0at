/**
 * @file data_builder.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements the functions for managing data building in the Goat virtual machine.
 *
 * This file implements the logic for adding and managing static data in the Goat virtual machine's
 * bytecode, including managing data descriptors, raw data storage, and ensuring uniqueness
 * of strings using an AVL tree.
 */

#include <stdlib.h>
#include <memory.h>

#include "data_builder.h"
#include "lib/allocate.h"

/**
 * @brief Initial capacity for the descriptors list
 */
#define INITIAL_DESCRIPTORS_LIST_CAPACITY 16

/**
 * @brief Initial capacity for the data array
 */
#define INITIAL_DATA_ARRAY_CAPACITY 256

data_builder_t *data_builder_create(void) {
    data_builder_t *builder = (data_builder_t*)ALLOC(sizeof(data_builder_t));
    builder->descriptors = (data_descriptor_t*)ALLOC(INITIAL_DESCRIPTORS_LIST_CAPACITY * sizeof(data_descriptor_t));
    builder->data = (uint8_t*)ALLOC(INITIAL_DATA_ARRAY_CAPACITY * sizeof(uint8_t));
    builder->data_size = 0;
    builder->data_capacity = INITIAL_DATA_ARRAY_CAPACITY;
    builder->descriptors_count = 0;
    builder->descriptors_capacity = INITIAL_DESCRIPTORS_LIST_CAPACITY;
    builder->strings = avl_tree_create((int(*)(void*, void*))wcscmp);
    return builder;
}

uint32_t data_builder_add(data_builder_t *builder, void *data, size_t size) {
    size_t aligned_size = (size + 3) & ~3;
    size_t new_size = builder->data_size + aligned_size;
    if (new_size > builder->data_capacity) {
        size_t new_capacity = builder->data_capacity * 2;
        if (new_capacity < new_size) {
            new_capacity = new_size;
        }
        builder->data_capacity = new_capacity;
        uint8_t *new_data = (uint8_t*)ALLOC(new_size);
        memcpy(new_data, builder->data, builder->data_size);
        FREE(builder->data);
        builder->data = new_data;
    }
    size_t offset = builder->data_size;
    builder->data_size += aligned_size;
    memcpy(&builder->data[offset], data, size);
    if (aligned_size > size) {
        memset(&builder->data[offset + size], 0, aligned_size - size);
    }
    if (builder->descriptors_count >= builder->descriptors_capacity) {
        builder->descriptors_capacity *= 2;
        data_descriptor_t *new_descriptors = 
            (data_descriptor_t *)ALLOC(builder->descriptors_capacity * sizeof(data_descriptor_t));
        for (size_t i = 0; i < builder->descriptors_count; ++i) {
            new_descriptors[i] = builder->descriptors[i];
        }
        FREE(builder->descriptors);
        builder->descriptors = new_descriptors;
    }
    uint32_t index = (uint32_t)builder->descriptors_count++;
    data_descriptor_t descriptor = {
        .index = index,
        .offset = (uint64_t)offset,
        .size = (uint32_t)aligned_size
    };
    builder->descriptors[index] = descriptor;
    return index;
}

uint32_t data_builder_add_string(data_builder_t *builder, wchar_t *string) {
    uint32_t *old_index = (uint32_t*)avl_tree_get(builder->strings, string);
    if (old_index) {
        return *old_index;
    }
    size_t len = (wcslen(string) + 1) * sizeof(wchar_t);
    uint32_t index = data_builder_add(builder, (void*)string, len);
    data_descriptor_t *descriptor = &builder->descriptors[index];
    avl_tree_set(builder->strings, &builder->data[descriptor->offset], &descriptor->index);
    return index;
}

void data_builder_destroy(data_builder_t *builder) {
    FREE(builder->descriptors);
    FREE(builder->data);
    avl_tree_destroy(builder->strings);
    FREE(builder);
}
