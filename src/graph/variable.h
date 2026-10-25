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

    /** @brief Name may be supplied or shadowed by runtime prototypes. */
    bool dynamic_lookup;
} variable_t;

/** @brief Checks variable writability without reading or changing its cell. */
instr_index_t
generate_variable_write_check(const node_t *node, code_builder_t *code, data_builder_t *data);
