/**
 * @file data_type.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the data type descriptor used by expressions.
 */

#pragma once

#include "lib/value.h"

typedef struct expression_t expression_t;

/** @brief Describes a semantic data type. */
typedef struct {
    /** @brief C language type equivalent (if any). */
    string_view_t c_equivalent;

    /**
     * @brief Prototype expression that defines this type (optional).
     *
     * May be NULL if the type has no defining prototype or if it is a built-in/implicit type.
     */
    struct expression_t *proto;

} data_type_t;

/** @brief Helper macro for defining built-in data types. */
#define BUILT_IN_DATA_TYPE(c_type_name)                                                            \
    {                                                                                              \
        .c_equivalent = {                                                                          \
            .data = (c_type_name),                                                                 \
            .length = sizeof(c_type_name) / sizeof(wchar_t) - 1                                    \
        }                                                                                          \
    }
