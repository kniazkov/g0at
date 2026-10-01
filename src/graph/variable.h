/**
 * @file variable.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the variable expression node.
 */

#pragma once

#include "assignable_expression.h"
#include "declarations.h"

/** @brief A variable expression node. */
typedef struct variable_t {
    /** @brief Base expression structure from which variable_t inherits. */
    assignable_expression_t base;

    /** @brief String representing the variable's name. */
    string_view_t name;

    /**
     * @brief Pointer to the declarator corresponding to this variable usage.
     *
     * May be NULL if the variable is unresolved.
     */
    declarator_t *declarator;
} variable_t;
