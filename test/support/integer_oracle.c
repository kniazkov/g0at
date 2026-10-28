/** @file integer_oracle.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only sign/magnitude arithmetic using portable 32-bit limbs.
 */
#include "test/support/integer_oracle.h"

#include <stdlib.h>

static integer_oracle_value_t normalize(integer_oracle_value_t value) {
    if (!(value.limb[0] | value.limb[1] | value.limb[2] | value.limb[3]))
        value.negative = false;
    return value;
}

static integer_oracle_value_t from_int64(int64_t input) {
    /* Conversion to unsigned is defined modulo 2^64, including INT64_MIN.
     * Unsigned subtraction obtains its magnitude without signed negation. */
    uint64_t magnitude = input < 0 ? UINT64_C(0) - (uint64_t)input : (uint64_t)input;
    integer_oracle_value_t value = {
        input < 0, {(uint32_t)magnitude, (uint32_t)(magnitude >> 32), 0, 0}
    };
    return value;
}

static int compare_magnitudes(integer_oracle_value_t a, integer_oracle_value_t b) {
    for (int i = 3; i >= 0; --i) {
        if (a.limb[i] != b.limb[i])
            return a.limb[i] > b.limb[i] ? 1 : -1;
    }
    return 0;
}

static integer_oracle_value_t add(integer_oracle_value_t a, integer_oracle_value_t b) {
    integer_oracle_value_t result = {a.negative, {0, 0, 0, 0}};
    uint64_t carry = 0;
    if (a.negative == b.negative) {
        for (int i = 0; i < 4; ++i) {
            uint64_t sum = (uint64_t)a.limb[i] + b.limb[i] + carry;
            result.limb[i] = (uint32_t)sum;
            carry = sum >> 32;
        }
    } else {
        if (compare_magnitudes(a, b) < 0) {
            integer_oracle_value_t temporary = a;
            a = b;
            b = temporary;
            result.negative = a.negative;
        }
        for (int i = 0; i < 4; ++i) {
            uint64_t subtrahend = (uint64_t)b.limb[i] + carry;
            result.limb[i] = (uint32_t)((uint64_t)a.limb[i] - subtrahend);
            carry = (uint64_t)a.limb[i] < subtrahend;
        }
    }
    return normalize(result);
}

static integer_oracle_value_t multiply(integer_oracle_value_t a,
                                       integer_oracle_value_t b) {
    integer_oracle_value_t result = {a.negative != b.negative, {0, 0, 0, 0}};
    /* Inputs originate from int64, hence only their two low limbs are nonzero.
     * A product plus one result limb plus carry is at most UINT64_MAX. */
    for (int i = 0; i < 2; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < 2; ++j) {
            uint64_t product = (uint64_t)a.limb[i] * b.limb[j]
                + result.limb[i + j] + carry;
            result.limb[i + j] = (uint32_t)product;
            carry = product >> 32;
        }
        result.limb[i + 2] = (uint32_t)carry;
    }
    return normalize(result);
}

integer_oracle_value_t integer_oracle_evaluate(integer_oracle_operation_t operation,
                                              int64_t a, int64_t b) {
    integer_oracle_value_t left = from_int64(a);
    integer_oracle_value_t right = from_int64(b);
    switch (operation) {
        case INTEGER_ORACLE_ADD:
            return add(left, right);
        case INTEGER_ORACLE_SUB:
            right.negative = !right.negative;
            return add(left, right);
        case INTEGER_ORACLE_MUL:
            return multiply(left, right);
        case INTEGER_ORACLE_NEG:
            left.negative = !left.negative;
            return normalize(left);
        case INTEGER_ORACLE_ABS:
            left.negative = false;
            return left;
        case INTEGER_ORACLE_INC:
            return add(left, from_int64(1));
        case INTEGER_ORACLE_DEC:
            return add(left, from_int64(-1));
    }
    abort();
}

int64_t integer_oracle_saturate(integer_oracle_value_t value) {
    if (value.limb[2] || value.limb[3])
        return value.negative ? INT64_MIN : INT64_MAX;
    uint64_t magnitude = ((uint64_t)value.limb[1] << 32) | value.limb[0];
    if (magnitude > (uint64_t)INT64_MAX)
        return value.negative ? INT64_MIN : INT64_MAX;
    return value.negative ? -(int64_t)magnitude : (int64_t)magnitude;
}
