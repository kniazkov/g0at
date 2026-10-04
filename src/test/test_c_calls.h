/** @file test_c_calls.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiled static-call and binding validation tests.
 */
#pragma once
#include "codegen/source_builder.h"

bool append_c_call_tests(source_builder_t *output, source_builder_t *checks);
bool test_c_call_rejections(void);
