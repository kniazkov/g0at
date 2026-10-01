/**
 * @file linker.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides the functions for linking bytecode and static data into a single binary file for
 * the Goat virtual machine.
 */

#pragma once

#include "vm/bytecode.h"
#include "data_builder.h"
#include "code_builder.h"

/**
 * @brief Links code and data into a single bytecode structure.
 * @note The function dynamically allocates memory for the binary file. It is the caller's
 * responsibility to free this memory once it is no longer needed.
 */
bytecode_t *link_code_and_data(code_builder_t *code_builder, data_builder_t *data_builder);
