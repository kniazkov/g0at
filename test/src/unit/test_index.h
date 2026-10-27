/** @file test_index.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Indexed access regression tests.
 */
#pragma once
#include <stdbool.h>
/** @brief Mixed postfix syntax and malformed index lists. */
bool test_index_parser(void);
/** @brief Dedicated opcodes, arbitrary index lists and persisted bytecode. */
bool test_index_dispatch(void);
/** @brief Object keys, cloning, constants, collection and cycle rendering. */
bool test_index_model(void);
