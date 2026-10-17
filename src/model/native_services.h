/** @file native_services.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Interpreter-owned state behind the native service ABI.
 */
#pragma once
#include "goat/native_abi.h"

typedef struct native_memory_block_t native_memory_block_t;

/** @brief Zero-initialize for each outer call; do not copy while blocks are live. */
typedef struct native_memory_t {
    native_memory_block_t *first;
} native_memory_t;

/** @brief Binds empty state to interpreter callbacks, without allocating memory. */
goat_native_context_v2_t init_native_context(native_memory_t *memory);
