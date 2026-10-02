/** @file comparison.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact numeric and string comparisons.
 */
#include "comparison.h"

#include <limits.h>
#include <math.h>

comparison_order_t compare_integer_real(int64_t integer, double real) {
    if (isnan(real))
        return ORDER_UNORDERED;
    if (real >= 0x1p63)
        return ORDER_LESS;
    if (real < -0x1p63)
        return ORDER_GREATER;
    int64_t truncated = (int64_t)real;
    if (integer < truncated)
        return ORDER_LESS;
    if (integer > truncated)
        return ORDER_GREATER;
    double whole;
    double fraction = modf(real, &whole);
    return fraction > 0 ? ORDER_LESS : fraction < 0 ? ORDER_GREATER : ORDER_EQUAL;
}

comparison_order_t compare_reals(double left, double right) {
    if (isnan(left) || isnan(right))
        return ORDER_UNORDERED;
    return left < right ? ORDER_LESS : left > right ? ORDER_GREATER : ORDER_EQUAL;
}

static uint32_t next_scalar(string_view_t str, size_t *index) {
    uint32_t value = (uint32_t)str.data[(*index)++];
#if WCHAR_MAX <= 0xffff
    if (value >= 0xd800 && value <= 0xdbff && *index < str.length) {
        uint32_t low = (uint32_t)str.data[*index];
        if (low >= 0xdc00 && low <= 0xdfff) {
            (*index)++;
            value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
        }
    }
#endif
    return value;
}

comparison_order_t compare_strings(string_view_t left, string_view_t right) {
    size_t i = 0, j = 0;
    while (i < left.length && j < right.length) {
        uint32_t a = next_scalar(left, &i), b = next_scalar(right, &j);
        if (a != b)
            return a < b ? ORDER_LESS : ORDER_GREATER;
    }
    return i < left.length ? ORDER_GREATER : j < right.length ? ORDER_LESS : ORDER_EQUAL;
}

bool comparison_matches(comparison_order_t order, comparison_kind_t kind) {
    switch (kind) {
        case COMPARE_LESS:
            return order == ORDER_LESS;
        case COMPARE_LEQ:
            return order == ORDER_LESS || order == ORDER_EQUAL;
        case COMPARE_GREATER:
            return order == ORDER_GREATER;
        case COMPARE_GREQ:
            return order == ORDER_GREATER || order == ORDER_EQUAL;
        case COMPARE_EQUAL:
            return order == ORDER_EQUAL;
        case COMPARE_NOT_EQUAL:
            return order != ORDER_EQUAL;
    }
    return false;
}
