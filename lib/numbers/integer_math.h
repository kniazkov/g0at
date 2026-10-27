/** @file integer_math.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defined arithmetic for signed 64-bit language integers.
 */
#pragma once
#include <stdint.h>

/** @brief Selects overflow intrinsics when supported; tests may force the portable path. */
#if !defined(GOAT_FORCE_PORTABLE_INTEGER_MATH)
#    if defined(__has_builtin)
#        if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow)         \
            && __has_builtin(__builtin_mul_overflow)
#            define GOAT_INTEGER_OVERFLOW_BUILTINS 1
#        endif
#    elif defined(__GNUC__) && __GNUC__ >= 5
#        define GOAT_INTEGER_OVERFLOW_BUILTINS 1
#    endif
#endif

/** @brief Adds with saturation at the signed 64-bit boundaries. */
static inline int64_t add_int64_saturating(int64_t a, int64_t b) {
#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_add_overflow(a, b, &result))
        return a >= 0 ? INT64_MAX : INT64_MIN;
    return result;
#else
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
#endif
}

/** @brief Subtracts with saturation without negating the second operand. */
static inline int64_t subtract_int64_saturating(int64_t a, int64_t b) {
#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_sub_overflow(a, b, &result))
        return a >= 0 ? INT64_MAX : INT64_MIN;
    return result;
#else
    if (b < 0 && a > INT64_MAX + b)
        return INT64_MAX;
    if (b > 0 && a < INT64_MIN + b)
        return INT64_MIN;
    return a - b;
#endif
}

/** @brief Multiplies with saturation; the portable path checks unsigned magnitudes. */
static inline int64_t multiply_int64_saturating(int64_t a, int64_t b) {
#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_mul_overflow(a, b, &result))
        return (a < 0) == (b < 0) ? INT64_MAX : INT64_MIN;
    return result;
#else
    uint64_t x = a < 0 ? UINT64_C(0) - (uint64_t)a : (uint64_t)a;
    uint64_t y = b < 0 ? UINT64_C(0) - (uint64_t)b : (uint64_t)b;
    int negative = (a < 0) != (b < 0);
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    if (y != 0 && x > limit / y)
        return negative ? INT64_MIN : INT64_MAX;
    uint64_t product = x * y;
    if (negative && product == (uint64_t)INT64_MAX + 1)
        return INT64_MIN;
    return negative ? -(int64_t)product : (int64_t)product;
#endif
}

/** @brief Rounds an integer to double before arithmetic, including on x87 targets. */
static inline double integer_to_double(int64_t value) {
    volatile double rounded = (double)value;
    return rounded;
}
