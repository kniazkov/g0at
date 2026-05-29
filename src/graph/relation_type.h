/**
 * @file relation_type.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Relation types between AST nodes.
 *
 * This file defines non-child relations between AST nodes. Such relations are
 * used by semantic analysis and graph visualization to connect nodes that are
 * related logically, but are not connected through the syntax tree hierarchy.
 */

#pragma once

#include "lib/value.h"

/**
 * @enum relation_type_t
 * @brief Enumeration of non-child relations between AST nodes.
 *
 * A relation describes why one node refers to another node outside the normal
 * parent-child AST structure.
 */
typedef enum {
    /**
     * @brief No relation.
     *
     * Used as a fallback value when a relation is absent or invalid.
     */
    RELATION_NONE = 0,

    /**
     * @brief Declaration relation.
     *
     * Connects a node that uses a named entity to the node that declares it.
     * For example, NODE_VARIABLE can refer to NODE_VARIABLE_DECLARATOR,
     * NODE_CONSTANT_DECLARATOR, or NODE_ARGUMENT.
     */
    RELATION_DECLARATION,
} relation_type_t;

/**
 * @brief Converts a relation type to a string value.
 *
 * @param type Relation type to convert.
 * @return Static string value containing the relation type name.
 */
static inline string_value_t relation_type_to_string(relation_type_t type) {
    switch (type) {
        case RELATION_DECLARATION:
            return STATIC_STRING(L"declaration");

        default:
            return STATIC_STRING(L"none");
    }
}
