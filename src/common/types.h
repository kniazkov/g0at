/**
 * @file types.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of shared types for the Goat language compiler and runtime.
 */

#pragma once

#include <stddef.h>

/** @brief Type for indexing the instruction list. */
typedef size_t instr_index_t;

/**
 * @brief Sentinel value representing an invalid instruction index.
 *
 * Used to indicate that an instruction index is not valid or uninitialized.
 */
#define BAD_INSTR_INDEX SIZE_MAX

/** @brief Type for indexing elements on the object stack. */
typedef size_t stack_index_t;

/**
 * @brief Sentinel value representing an invalid stack index.
 *
 * Used to signify an invalid or uninitialized stack index. This value should never correspond to an
 * actual position in the object stack.
 */
#define BAD_STACK_INDEX SIZE_MAX
