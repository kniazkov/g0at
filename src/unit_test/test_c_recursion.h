/** @file test_c_recursion.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Standalone recursive C execution and self-binding validation.
 */
#pragma once
#include "codegen/source_builder.h"

bool append_c_recursion_tests(source_builder_t *output, source_builder_t *checks);
bool test_c_recursive_bindings(void);
