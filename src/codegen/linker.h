/**
 * @file linker.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides the functions for linking bytecode and static data into a single binary file for
 * the Goat virtual machine.
 */

#pragma once

#include "code_builder.h"
#include "data_builder.h"
#include "vm/bytecode.h"

/**
 * @brief Links code and data into a single bytecode structure.
 * The builders remain caller-owned and may be destroyed after linking. The
 * returned image owns its copied instructions/data; release it with free_bytecode().
 * This standalone wrapper aborts if linking cannot produce a valid-sized image;
 * the module variant instead reports invalid arguments/size by returning NULL.
 */
bytecode_t *link_code_and_data(code_builder_t *code_builder, data_builder_t *data_builder);

/** @brief Link a module with explicit contiguous file instruction ranges.
 * Returns NULL for invalid arguments or a module exceeding the v1 image size limit.
 */
bytecode_t *link_module_code_and_data(code_builder_t *code,
                                      data_builder_t *data,
                                      const bytecode_file_t *files,
                                      size_t count);
