/**
 * @file common_types.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of shared types for the Goat language runtime.
 * 
 * This file contains type declarations used throughout the Goat model 
 * implementation.
 */

#pragma once

#include <stddef.h>

/**
 * @typedef instr_index_t
 * @brief Type for indexing the instruction list.
 * 
 * The instruction index is used to keep track of the current instruction that the thread
 * is executing.
 */
typedef size_t instr_index_t;

/**
 * @def BAD_INSTR_INDEX
 * @brief Sentinel value representing an invalid instruction index.
 *
 * Used to indicate that an instruction index is not valid or uninitialized.
 * Guaranteed not to match any valid index in the instruction list.
 */
#define BAD_INSTR_INDEX SIZE_MAX

/**
 * @typedef stack_index_t
 * @brief Type for indexing elements on the object stack.
 * 
 * Used to unambiguously reference the position of an element within the stack.
 */
typedef size_t stack_index_t;

/**
 * @def BAD_STACK_INDEX
 * @brief Sentinel value representing an invalid stack index.
 *
 * Used to signify an invalid or uninitialized stack index. This value should
 * never correspond to an actual position in the object stack.
 */
#define BAD_STACK_INDEX SIZE_MAX
