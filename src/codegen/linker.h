/**
 * @file linker.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides the functions for linking bytecode and static data into a single binary file
 *        for the Goat virtual machine.
 *
 * This file defines the necessary functions to combine bytecode instructions and static data
 * into one unified  structure. It allows the creation of a complete binary file that can be
 * executed by the Goat virtual machine.
 *
 * @note Ensure that all memory allocated during the linking process is freed
 *       when no longer needed.
 */

#pragma once

#include "vm/bytecode.h"
#include "data_builder.h"
#include "code_builder.h"

/**
 * @brief Links code and data into a single bytecode structure.
 *
 * This function combines the bytecode instructions from the code builder and the data descriptors
 * and data from the data builder into a single binary format. It creates a `bytecode_t` structure
 * containing pointers to the instructions, data descriptors, and actual data in the binary file.
 * The function also sets up the binary header with the correct offsets for each section.
 *
 * @param code_builder A pointer to the code builder containing the instructions.
 * @param data_builder A pointer to the data builder containing the data and data descriptors.
 * @return A `bytecode_t` structure that contains the combined bytecode, with pointers
 *         to the instructions, data descriptors, and data in the binary file.
 *
 * @note The function dynamically allocates memory for the binary file.
 *       It is the caller's responsibility to free this memory once it is no longer needed.
 */
bytecode_t *link_code_and_data(code_builder_t *code_builder, data_builder_t *data_builder);
