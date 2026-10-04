/** @file test_binary.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Persisted bytecode integrity and option tests.
 */
#pragma once
#include <stdbool.h>
bool test_binary_roundtrip(void);
bool test_binary_rejection(void);
bool test_binary_options(void);
