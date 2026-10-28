/** @file integer_oracle.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent exact integer arithmetic for test expectations only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum integer_oracle_operation {
    INTEGER_ORACLE_ADD,
    INTEGER_ORACLE_SUB,
    INTEGER_ORACLE_MUL,
    INTEGER_ORACLE_NEG,
    INTEGER_ORACLE_ABS,
    INTEGER_ORACLE_INC,
    INTEGER_ORACLE_DEC
} integer_oracle_operation_t;

/** @brief Sign and unsigned 128-bit magnitude, least-significant limb first.
 * Results have canonical nonnegative zero. No production arithmetic helper or
 * compiler extended integer type is used to calculate this representation.
 */
typedef struct {
    bool negative;
    uint32_t limb[4];
} integer_oracle_value_t;

/** @brief Evaluate exactly, before saturation. Unary operations ignore b.
 * operation must be one of the enumerators above; an invalid value aborts.
 * Every result of the supported int64 operations fits in this representation.
 */
integer_oracle_value_t
integer_oracle_evaluate(integer_oracle_operation_t operation, int64_t a, int64_t b);

/** @brief Clamp an exact signed magnitude to the int64 range without overflow. */
int64_t integer_oracle_saturate(integer_oracle_value_t value);
