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
