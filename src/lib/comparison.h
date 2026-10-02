/** @file comparison.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared primitive comparison semantics.
 */
#pragma once
#include "value.h"

typedef enum {
    COMPARE_LESS,
    COMPARE_LEQ,
    COMPARE_GREATER,
    COMPARE_GREQ,
    COMPARE_EQUAL,
    COMPARE_NOT_EQUAL
} comparison_kind_t;

typedef enum {
    ORDER_LESS = 1,
    ORDER_EQUAL = 2,
    ORDER_GREATER = 4,
    ORDER_UNORDERED = 8
} comparison_order_t;

/** @brief Compares an integer exactly against a double, including NaN. */
comparison_order_t compare_integer_real(int64_t integer, double real);
comparison_order_t compare_reals(double left, double right);
/** @brief Lexicographic Unicode scalar comparison, respecting string lengths. */
comparison_order_t compare_strings(string_view_t left, string_view_t right);
bool comparison_matches(comparison_order_t order, comparison_kind_t kind);
