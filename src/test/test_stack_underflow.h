/** @file test_stack_underflow.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Process-isolated checks of fatal stack errors.
 */
#pragma once
#include <stdbool.h>

bool test_stack_underflow(const char *executable);
void run_stack_underflow_case(int index);
