/** @file native_pipeline.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional compilation, loading and binding before analysis storage is released.
 */
#pragma once
#include "graph/node.h"
#include "vm/bytecode.h"

typedef enum {
    NATIVE_PREPARE_READY,
    NATIVE_PREPARE_EMPTY,
    NATIVE_PREPARE_IO_ERROR,
    NATIVE_PREPARE_COMPILE_ERROR,
    NATIVE_PREPARE_LOAD_ERROR,
    NATIVE_PREPARE_BIND_ERROR
} native_prepare_status_t;

typedef struct {
    native_prepare_status_t status;
    size_t bound_functions;
    size_t omitted_specializations;
    char *diagnostic;
} native_prepare_result_t;

/** @brief Attaches only a validated generated module; failures leave fresh bytecode untouched. */
native_prepare_result_t
prepare_native_execution(const node_t *root, bytecode_t *code, const char *compiler);
void destroy_native_prepare_result(native_prepare_result_t *result);
