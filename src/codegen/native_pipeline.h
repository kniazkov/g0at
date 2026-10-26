/** @file native_pipeline.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional compilation, loading and binding before analysis storage is released.
 */
#pragma once
#include "graph/node.h"
#include "native_compiler.h"
#include "vm/bytecode.h"

typedef enum {
    NATIVE_PREPARE_READY,
    NATIVE_PREPARE_EMPTY,
    NATIVE_PREPARE_IO_ERROR,
    NATIVE_PREPARE_COMPILE_ERROR,
    NATIVE_PREPARE_LOAD_ERROR,
    NATIVE_PREPARE_BIND_ERROR,
    NATIVE_PREPARE_CLEANUP_ERROR,
    NATIVE_PREPARE_UNAVAILABLE
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

/** @brief Saves a validated library and retains its bindings for bytecode serialization. */
native_prepare_result_t prepare_native_artifact(const node_t *root,
                                                bytecode_t *code,
                                                const char *compiler,
                                                const char *destination);

/** @brief Shares the owner's lazy cleaner with discovery and all temporary consumers. */
native_prepare_result_t
prepare_native_execution_in_context(const node_t *root,
                                    bytecode_t *code,
                                    const char *compiler,
                                    native_build_context_t *context,
                                    const native_compile_options_t *options);
native_prepare_result_t prepare_native_artifact_in_context(const node_t *root,
                                                           bytecode_t *code,
                                                           const char *compiler,
                                                           const char *destination,
                                                           native_build_context_t *context,
                                                           const native_compile_options_t *options);

/** @brief Direct-run policy: probe only when own generated functions exist, sharing one cleaner. */
native_prepare_result_t prepare_native_discovered(const node_t *root,
                                                  bytecode_t *code,
                                                  const char *compiler,
                                                  const char *destination,
                                                  native_build_context_t *context,
                                                  uint32_t probe_timeout);
