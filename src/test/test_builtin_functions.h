/** @file test_builtin_functions.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native registry, abstract domains, and exception contracts.
 */
#pragma once
#include <stdbool.h>
bool test_builtin_registry();
bool test_builtin_numeric_results();
bool test_builtin_errors();
bool test_builtin_domains();

bool test_abs_domains();
