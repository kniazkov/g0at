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

/** @brief Rendered container/access node names use spaces. */
bool test_array_graph_labels(void);

/** @brief Growth, cycle lifetimes, removal ownership and virtual comparison failures. */
bool test_array_methods(void);
