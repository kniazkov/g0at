/**
 * @file variable.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the variable expression node.
 *
 * This file defines the structure representing a variable expression in the
 * abstract syntax tree (AST).
 */

#pragma once

#include "assignable_expression.h"
#include "declarations.h"

/**
 * @struct variable_t
 * @brief Represents a variable expression node.
 *
 * This structure defines a variable expression in the syntax tree. A variable refers
 * to a named entity, such as a variable or constant in the source code.
 *
 * During static analysis, each variable usage can be bound to its corresponding
 * declaration via the @ref declarator field.
 */
typedef struct variable_t {
    /**
     * @brief Base expression structure from which variable_t inherits.
     */
    assignable_expression_t base;

    /**
     * @brief String representing the variable's name.
     */
    string_view_t name;

    /**
     * @brief Pointer to the declarator corresponding to this variable usage.
     *
     * This field is populated during static analysis and links the variable
     * usage to its declaration in the syntax tree.
     * May be NULL if the variable is unresolved.
     */
    declarator_t *declarator;
} variable_t;
