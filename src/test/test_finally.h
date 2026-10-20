/** @file test_finally.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Cleanup grammar, persistence and GC regressions.
 */
#pragma once
#include <stdbool.h>
bool test_finally_parser(void);
bool test_finally_gc(void);
bool test_finally_binary(void);
