/**
 * @file data_builder.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Contains the declaration of the data builder structure and functions for managing
 * static data in the Goat virtual machine.
 *
 * This file defines the `data_builder_t` structure and functions for adding and managing
 * static data. It provides the ability to add data and track its descriptors, ensuring proper
 * alignment and structure.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "vm/bytecode.h"
#include "lib/avl_tree.h"

/**
 * @struct data_builder_t
 * @brief A builder for managing and adding static data to the bytecode file.
 *
 * This structure manages two dynamic arrays:
 * - One for storing `data_descriptor_t` structures, which describe the data.
 * - One for storing the actual data as raw bytes.
 *
 * The `data_builder_t` also contains an AVL tree to track strings and prevent duplicate entries.
 */
typedef struct {
    /**
     * @brief Array of data descriptors.
     */
    data_descriptor_t *descriptors;

    /**
     * @brief Array of raw data (with alignment).
     */
    uint8_t *data;

    /**
     * @brief size of the data array.
     */
    size_t data_size;

    /**
     * @brief Capacity of the data array.
     */
    size_t data_capacity;
    
    /**
     * @brief Number of data descriptors.
     */
    size_t descriptors_count;

    /**
     * @brief Capacity of the descriptor array.
     */
    size_t descriptors_capacity;

    /**
     * @brief AVL tree for tracking added strings to prevent duplicates.
     */
    avl_tree_t *strings;
} data_builder_t;

/**
 * @brief Creates a new data builder with a default initial capacity.
 *
 * This function initializes a new `data_builder_t` with a fixed initial capacity:
 * - 16 data descriptors.
 * - 256 bytes of raw data.
 *
 * @return A pointer to the created data builder.
 */
data_builder_t *create_data_builder(void);

/**
 * @brief Adds a piece of data to the builder.
 *
 * This function adds the given data to the builder, appending it to the raw data array,
 * and creates a new descriptor for the data. The data is aligned to 4-byte boundaries.
 * The function returns the index of the added data descriptor, which can later be used in
 * bytecode instructions to address the data.
 *
 * @param builder The data builder to which the data will be added.
 * @param data The data to be added (as a pointer to void).
 * @param size The size of the data in bytes.
 * @return The index of the added data descriptor.
 */
uint32_t add_data_to_data_segment(data_builder_t *builder, void *data, size_t size);

/**
 * @brief Adds a string to the builder.
 *
 * This function adds a string (wide-character) to the builder.
 * It internally calls `add_data_to_data_segment` to handle the data and prevent duplicate entries
 * by using the AVL tree.
 *
 * @param builder The data builder to which the string will be added.
 * @param string The wide-character string to be added.
 * @return The index of the added data descriptor.
 */
uint32_t add_string_to_data_segment(data_builder_t *builder, wchar_t *string);

/**
 * @brief Destroys the data builder and frees its memory.
 *
 * This function frees the memory used by the data builder's descriptor array, data array,
 * and the AVL tree used for string tracking. After calling this function, the builder should
 * no longer be used.
 *
 * @param builder The data builder to be destroyed.
 */
void destroy_data_builder(data_builder_t *builder);
