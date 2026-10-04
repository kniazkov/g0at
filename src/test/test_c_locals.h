/** @file test_c_locals.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiled local storage and assignment checks.
 */
#pragma once
#include "codegen/source_builder.h"

bool append_c_local_tests(source_builder_t *output, source_builder_t *checks);
bool test_c_local_rejections(void);
