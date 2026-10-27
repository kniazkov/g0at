/** @file test_c_control.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiled comparisons and branch fixtures.
 */
#pragma once
#include "codegen/source_builder.h"

bool append_c_control_tests(source_builder_t *output, source_builder_t *checks);

bool test_c_control_rejections(void);
