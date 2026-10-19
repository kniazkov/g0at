/** @file test_array.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Array syntax and lifetime regressions.
 */
#pragma once
#include <stdbool.h>
/** @brief Literal/postfix combinations, trailing commas and invalid lists. */
bool test_array_parser(void);
/** @brief Ownership, aliasing, cloning, bounds and mixed cycle collection. */
bool test_array_model(void);
