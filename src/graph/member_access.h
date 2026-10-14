/**
 * @file member_access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the member access expression node.
 */

#pragma once

#include "assignable_expression.h"

/**
 * @brief A member access expression node, e.g. `object.property`.
 *
 * The node is an assignable expression: reading a property is performed via `generate_bytecode`,
 * and writing via `generate_bytecode_assign`.
 */
typedef struct member_access_t {
    /** @brief Base assignable expression structure from which member_access_t inherits. */
    assignable_expression_t base;

    /** @brief The object whose member is accessed. */
    expression_t *object;

    /** @brief The name of the accessed member. */
    string_view_t name;
} member_access_t;

/** @brief Creates a new member access expression node. */
expression_t *create_member_access_node(arena_t *arena, expression_t *object, string_view_t name);
