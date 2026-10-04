/** @file c_adapter.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric ABI declarations, adapters and module descriptors.
 */
#pragma once
#include "c_module.h"

void c_emit_native_abi(source_builder_t *builder);
void c_emit_adapter(source_builder_t *builder, const c_module_function_t *entry);
void c_emit_native_module(source_builder_t *builder,
                          const c_module_t *module,
                          const c_generation_result_t *results,
                          size_t count);

/** @brief Per-adapter recovery boundary shared by all typed calls in one module. */
void c_emit_native_guard(source_builder_t *builder);
