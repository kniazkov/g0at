/** @file test_function_analysis.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Function-domain and bounded-call regression tests.
 */
#pragma once
#include <stdbool.h>

bool test_function_lattice();
bool test_function_call_budget();
bool test_function_call_state();
