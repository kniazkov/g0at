/** @file native_services.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Interpreter-owned state behind the native service ABI.
 */
#pragma once
#include "goat/native_abi.h"

#include <stdbool.h>

typedef struct native_memory_block_t native_memory_block_t;

/** @brief Zero-initialize for each outer call; do not copy while blocks are live. */
typedef struct native_memory_t {
    native_memory_block_t *first;
    struct thread_t *thread; /**< Optional owning VM call for host termination. */
    uint32_t(GOAT_NATIVE_CALL *exit_callback)(void *state,
                                              uint32_t count,
                                              uint32_t type,
                                              int64_t code); /**< Optional VM-owned binding. */
    bool host_exception; /**< Rejected host exit; propagate without native replay. */
} native_memory_t;

/** @brief Binds empty state to interpreter callbacks, without allocating memory. */
goat_native_context_v2_t init_native_context(native_memory_t *memory);
