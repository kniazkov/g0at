/**
 * @file assignable_expression.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the assignable expression structure.
 */

#pragma once

#include "expression.h"

typedef struct assignable_expression_t assignable_expression_t;

/** @brief The structure representing an assignable expression node, extending `expression_t`. */
struct assignable_expression_t {
    /** @brief Base expression node, providing common expression attributes. */
    expression_t base;
};
