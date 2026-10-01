/**
 * @file relation_type.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Relation types between AST nodes.
 */

#pragma once

#include "lib/value.h"

/** @brief Enumeration of non-child relations between AST nodes. */
typedef enum {
    /** @brief No relation. */
    RELATION_NONE = 0,

    /** @brief Declaration relation. */
    RELATION_DECLARATION,
} relation_type_t;

/** @brief Converts a relation type to a string value. */
static inline string_value_t relation_type_to_string(relation_type_t type) {
    switch (type) {
        case RELATION_DECLARATION:
            return STATIC_STRING(L"declaration");

        default:
            return STATIC_STRING(L"none");
    }
}
