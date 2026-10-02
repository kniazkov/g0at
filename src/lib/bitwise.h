/** @file bitwise.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defined operations on 64-bit two's-complement bit patterns.
 */
#pragma once
#include <stdint.h>

typedef enum { BIT_AND, BIT_OR, BIT_XOR, BIT_SHIFT_LEFT, BIT_SHIFT_RIGHT } bitwise_kind_t;

static inline int64_t integer_from_bits(uint64_t bits) {
    return bits <= INT64_MAX ? (int64_t)bits : INT64_MIN + (int64_t)(bits - (UINT64_C(1) << 63));
}

static inline int64_t invert_integer(int64_t value) {
    return integer_from_bits(~(uint64_t)value);
}

/** @brief Shift callers must validate that right is in 0..63. */
static inline int64_t bitwise_integer(int64_t left, int64_t right, bitwise_kind_t kind) {
    uint64_t a = (uint64_t)left, b = (uint64_t)right;
    switch (kind) {
        case BIT_AND:
            return integer_from_bits(a & b);
        case BIT_OR:
            return integer_from_bits(a | b);
        case BIT_XOR:
            return integer_from_bits(a ^ b);
        case BIT_SHIFT_LEFT:
            return integer_from_bits(a << b);
        case BIT_SHIFT_RIGHT:
            return integer_from_bits((a >> b) | (left < 0 && b ? UINT64_MAX << (64 - b) : 0));
    }
    return 0;
}
