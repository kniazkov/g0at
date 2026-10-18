/** @file c_string_methods.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Typed string method dispatch shared by analysis and native emission.
 */
#pragma once
#include "c_generation.h"
#include "model/builtin_function.h"

const builtin_function_t *find_native_string_method(string_view_t name);
bool is_native_string_method(const builtin_function_t *builtin);
/** @brief Recognizes only a direct member call with a proven immutable string receiver. */
const builtin_function_t *resolve_native_string_method(const node_t *site,
                                                       const c_expression_context_t *context);
bool c_string_method_supported(const node_t *site,
                               const builtin_function_t *builtin,
                               const c_expression_context_t *context);
c_generated_expression_t c_string_method_call(const node_t *site,
                                              c_generation_context_t *context,
                                              const builtin_function_t *builtin);
c_generated_expression_t c_string_length(const node_t *node, c_generation_context_t *context);
