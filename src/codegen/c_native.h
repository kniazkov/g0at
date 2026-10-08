/** @file c_native.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Descriptor-keyed lowering of built-in functions.
 */
#pragma once
#include "c_generation.h"
#include "model/builtin_function.h"

/** @brief True when the descriptor has a registered C generator. */
bool c_native_builtin_supported(const builtin_function_t *builtin);

/** @brief Lowers a proven built-in call through its registered generator. */
c_generated_expression_t c_native_call(const node_t *node,
                                       c_generation_context_t *context,
                                       const builtin_function_t *builtin);
