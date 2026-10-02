/**
 * @file data_builder.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Contains the declaration of the data builder structure and functions for managing static
 * data in the Goat virtual machine.
 */

#pragma once

#include "lib/avl_tree.h"
#include "vm/bytecode.h"

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct data_builder_t data_builder_t;

/** @brief A builder for managing and adding static data to the bytecode file. */
struct data_builder_t {
    /** @brief Array of data descriptors. */
    data_descriptor_t *descriptors;

    /** @brief Array of raw data (with alignment). */
    uint8_t *data;

    /** @brief Size of the data array. */
    size_t data_size;

    /** @brief Capacity of the data array. */
    size_t data_capacity;

    /** @brief Number of data descriptors. */
    size_t descriptors_count;

    /** @brief Capacity of the descriptor array. */
    size_t descriptors_capacity;

    /** @brief AVL tree for tracking added strings to prevent duplicates. */
    avl_tree_t *strings;
};

/** @brief Creates a new data builder with a default initial capacity. */
data_builder_t *create_data_builder(void);

/**
 * @brief Adds a piece of data to the builder.
 *
 * The data is aligned to 4-byte boundaries.
 */
uint32_t add_data_to_data_segment(data_builder_t *builder, void *data, size_t size);

/** @brief Adds a string to the builder. */
uint32_t add_string_to_data_segment(data_builder_t *builder, const wchar_t *string);

/** @brief Adds a string to the data segment, using a pre-calculated length. */
uint32_t add_string_to_data_segment_ex(data_builder_t *builder, string_view_t string);

/**
 * @brief Destroys the data builder and frees its memory.
 *
 * After calling this function, the builder should no longer be used.
 */
void destroy_data_builder(data_builder_t *builder);
