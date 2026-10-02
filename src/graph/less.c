/** @file less.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Comparison node factory.
 */
#include "comparison.h"

expression_t *create_less_node(arena_t *arena, expression_t *left, expression_t *right) {
    return create_comparison_node(arena, COMPARE_LESS, left, right);
}
