/**
 * @file data_type.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the data type descriptor used by expressions.
 *
 * This file declares the @ref data_type_t structure that describes a semantic
 * data type known to the compiler. A data type may optionally be mapped to a
 * C language type and may optionally have a prototype expression that defines it.
 */

#pragma once
 
#include "lib/value.h"

/**
 * @typedef expression_t
 * @brief Forward declaration for the expression structure.
 */
typedef struct expression_t expression_t;

/**
 * @struct data_type_t
 * @brief Describes a semantic data type.
 *
 * This structure captures semantic information about a type known to the compiler.
 * It may be mapped to a C language type (for C codegen) and may reference a
 * prototype expression that defines the type.
 */
typedef struct {
    /**
     * @brief C language type equivalent (if any).
     *
     * A string view with the C type name that corresponds to this semantic type,
     * used for C code generation (e.g., "int32_t", "double", etc).
     * 
     * May be an empty string (length == 0). In that case, the type has no direct
     * mapping to a C type and cannot be emitted as a standalone C type.
     */
    string_view_t c_equivalent;

    /**
     * @brief Prototype expression that defines this type (optional).
     *
     * Points to an @ref expression_t that semantically defines the type (e.g.,
     * an object or another form of type prototype). May be NULL if the type has no defining
     * prototype or if it is a built-in/implicit type.
     */
    struct expression_t *proto;

    /* More fields may be added later (size, alignment, qualifiers, etc.). */
} data_type_t;

/**
 * @def BUILT_IN_DATA_TYPE
 * @brief Helper macro for defining built-in data types.
 *
 * This macro creates a @ref data_type_t initializer for a built-in type
 * that directly maps to a C language type name.
 *
 * @param c_type_name A wide string literal (e.g., L"int", L"double").
 */
#define BUILT_IN_DATA_TYPE(c_type_name) \
    {\
        .c_equivalent = { \
            .data = (c_type_name), \
            .length = sizeof(c_type_name) / sizeof(wchar_t) - 1 \
        }\
    }
